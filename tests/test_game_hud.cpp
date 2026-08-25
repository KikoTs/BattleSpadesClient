#include "battlespades/frontend/game_hud.hpp"
#include "battlespades/world/tutorial_lessons.hpp"
#include "battlespades/world/weapon_catalog.hpp"

#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

namespace {

using battlespades::frontend::ControlKeyNames;
using battlespades::frontend::GameHudInventorySlot;
using battlespades::frontend::GameHudModel;
using battlespades::frontend::GameHudPresentation;
using battlespades::frontend::GameHudPresentationContext;
using battlespades::frontend::resolve_control_placeholders;
using battlespades::frontend::retail_block_palette;
using battlespades::frontend::tutorial_string;
using battlespades::frontend::zombie_heartbeat_marker_size;
using battlespades::world::TutorialLessons;
using battlespades::world::TutorialLessonStage;

void expect(bool value, const char* message) {
    if (!value) {
        throw std::runtime_error{message};
    }
}

constexpr double fixed_dt{1.0 / 60.0};

[[nodiscard]] std::size_t sprite_count(const battlespades::ui::DrawList& list,
                                       std::string_view asset) {
    std::size_t count{};
    for (const auto& command : list.commands()) {
        const auto* sprite = std::get_if<battlespades::ui::SpriteDrawCommand>(&command);
        if (sprite != nullptr && sprite->asset_id == asset) {
            ++count;
        }
    }
    return count;
}

[[nodiscard]] bool contains_text(const battlespades::ui::DrawList& list, std::string_view needle) {
    for (const auto& command : list.commands()) {
        const auto* text = std::get_if<battlespades::ui::TextDrawCommand>(&command);
        if (text != nullptr && text->localization_key.find(needle) != std::string::npos) {
            return true;
        }
    }
    return false;
}

} // namespace

int main() {
    try {
        expect(battlespades::frontend::retail_jetpack_fuel_fraction(100.0) == 1.0 &&
                   battlespades::frontend::retail_jetpack_fuel_fraction(50.0) == 0.5,
               "retail 0..100 fuel units must normalize before drawing the gauge");
        expect(battlespades::frontend::retail_jetpack_fuel_fraction(-10.0) == 0.0 &&
                   battlespades::frontend::retail_jetpack_fuel_fraction(150.0) == 1.0 &&
                   battlespades::frontend::retail_jetpack_fuel_fraction(
                       std::numeric_limits<double>::quiet_NaN()) == 0.0,
               "jetpack HUD normalization must clamp and fail closed");
        // Compiled Player.update_heartbeat: 1.0 s hidden, a 0.1 s expansion,
        // then a one-second contraction before repeating.
        expect(zombie_heartbeat_marker_size(0.5) == 0.0,
               "Zombie survivor heart must wait through its off interval");
        expect(std::fabs(zombie_heartbeat_marker_size(1.0) - 25.6) < 0.001 &&
                   std::fabs(zombie_heartbeat_marker_size(1.1) - 30.72) < 0.001 &&
                   zombie_heartbeat_marker_size(2.1) == 0.0,
               "Zombie heart must reproduce the recovered 25.6..30.72px pulse");
        expect(battlespades::frontend::game_hud_assets::minimap_zombie_heart ==
                   "png/ui/heart_icon_256x256.png",
               "Zombie heartbeat must use the shipped retail heart art");

        // Placeholder resolution renders retail bracketed key names.
        {
            const ControlKeyNames names;
            expect(resolve_control_placeholders(tutorial_string("TUTORIAL_BASIC_CONTROLS_1"),
                                                names) == "Use [W], [A], [S] and [D] to move.",
                   "movement help must resolve to the retail bracketed keys");
            expect(resolve_control_placeholders(tutorial_string("TOOL_HELP_PANEL_CLOSE"), names) ==
                       "[H] Close",
                   "the close hint must resolve to the retail form");
            expect(resolve_control_placeholders("{key_unknown} test", names) == "[?] test",
                   "unknown placeholders must stay visible");
        }

        // Help panel animation: swap-out cue at set, appear cue after the
        // recovered 0.35 s delay, then the 8%%/frame ramp to 100.
        {
            GameHudModel model;
            model.set_help_messages(
                {"line one"}, "[H] Close", TutorialLessons::help_transition_delay);
            auto sounds = model.take_sounds();
            expect(sounds.play_disappear && !sounds.play_appear,
                   "setting text must queue the swap-out cue only");

            int appear_tick = -1;
            for (int tick{}; tick < 60; ++tick) {
                model.tick();
                if (model.take_sounds().play_appear) {
                    appear_tick = tick;
                    break;
                }
            }
            expect(appear_tick >= 20 && appear_tick <= 22,
                   "the appear cue must fire when the 0.35 s delay expires");
            expect(model.help_transition_percentage() == 0.0,
                   "the slide must not start before the delay expires");
            for (int tick{}; tick < 13; ++tick) {
                model.tick();
            }
            expect(model.help_transition_percentage() == 100.0,
                   "the retail ramp must reach 100%% in 13 frames");

            model.toggle_help();
            for (int tick{}; tick < 13; ++tick) {
                model.tick();
            }
            expect(model.help_transition_percentage() == 0.0,
                   "closing must ramp the panel away symmetrically");
        }

        // Retail death countdown audio is client-owned and keyed to integer
        // transitions of KillAction.respawn_time: beep2 at displayed 3 and 2,
        // then beep1 at displayed 1. Zero/immediate and 255/no-respawn never
        // emit these cues, and CreatePlayer clears any pending old-life beat.
        {
            GameHudModel model;
            model.set_respawn_time(5U);
            int earlier_beats{};
            int final_beats{};
            for (int tick{}; tick < 300; ++tick) {
                model.tick();
                const auto sounds = model.take_sounds();
                earlier_beats += sounds.play_respawn_beep2 ? 1 : 0;
                final_beats += sounds.play_respawn_beep1 ? 1 : 0;
            }
            expect(earlier_beats == 2 && final_beats == 1,
                   "respawn countdown must emit retail's beep2, beep2, beep1 sequence");

            model.set_respawn_time(0U);
            for (int tick{}; tick < 120; ++tick) {
                model.tick();
            }
            auto sounds = model.take_sounds();
            expect(!sounds.play_respawn_beep1 && !sounds.play_respawn_beep2,
                   "immediate mode respawns must not synthesize countdown audio");

            model.set_respawn_time(0xFFU);
            for (int tick{}; tick < 300; ++tick) {
                model.tick();
            }
            sounds = model.take_sounds();
            expect(!sounds.play_respawn_beep1 && !sounds.play_respawn_beep2,
                   "never-respawn state must remain silent");

            model.set_respawn_time(3U);
            for (int tick{}; tick < 61; ++tick) {
                model.tick();
            }
            model.clear_respawn();
            sounds = model.take_sounds();
            expect(!sounds.play_respawn_beep1 && !sounds.play_respawn_beep2,
                   "authoritative CreatePlayer must cancel a queued old-life countdown beat");
        }

        // Raw HP comes from the server. Retail's number then applies the
        // server-selected class durability profile without changing the bar.
        {
            GameHudModel model;
            model.set_health(100);
            model.set_health_damage_multiplier(0.6);
            expect(model.health() == 100 && model.displayed_health() == 167,
                   "Zombie raw 100 HP must display as 167 effective health");
            model.set_health(150);
            expect(model.health() == 150 && model.displayed_health() == 250,
                   "custom authoritative HP above 100 must not be truncated");
            model.set_health(6);
            expect(model.displayed_health() == 10 &&
                       model.health_text_color() ==
                           battlespades::ui::ColorRgba8{255U, 128U, 128U, 255U},
                   "low effective health must blend retail red toward white");
            model.set_health_damage_multiplier(0.0);
            expect(model.displayed_health() == 0,
                   "non-damageable class profiles must follow retail's zero readout");
        }

        // InitialInfo owns two independent HUD gates. Numeric HP hides only
        // the label; player-score visibility hides the SCORE frame and the
        // health-bar class portrait without removing the health bar itself.
        {
            GameHudModel model;
            const GameHudPresentation presentation;
            const auto context = GameHudPresentationContext{{800, 600}, 1'000U};
            const auto visible = presentation.build(model, context);
            expect(contains_text(visible, "100"),
                   "numeric HP must retain the local/tutorial default");

            std::vector<const battlespades::ui::TextDrawCommand*> health_draws;
            for (const auto& command : visible.commands()) {
                const auto* text =
                    std::get_if<battlespades::ui::TextDrawCommand>(&command);
                if (text != nullptr && text->localization_key == "100" &&
                    text->requested_font_size_pixels == 25.0) {
                    health_draws.push_back(text);
                }
            }
            expect(health_draws.size() == 3U,
                   "retail numeric HP must draw foreground, shadow, foreground");
            expect(health_draws[0U]->preferred_font_asset == "fonts/Spades.ttf" &&
                       std::fabs(health_draws[0U]->destination.x - 489.97) < 1e-9 &&
                       std::fabs(health_draws[0U]->destination.y - 578.5) < 1e-9 &&
                       health_draws[0U]->destination.width == 0.0 &&
                       health_draws[0U]->destination.height == 0.0 &&
                       health_draws[0U]->horizontal_alignment ==
                           battlespades::ui::HorizontalTextAlignment::center &&
                       health_draws[0U]->vertical_alignment ==
                           battlespades::ui::VerticalTextAlignment::baseline,
                   "primary HP draw must use Label's exact centred baseline");
            expect(std::fabs(health_draws[1U]->destination.x - 492.17) < 1e-9 &&
                       std::fabs(health_draws[1U]->destination.y - 580.7) < 1e-9 &&
                       health_draws[1U]->modulation.color ==
                           battlespades::ui::ColorRgba8{64U, 64U, 64U, 255U},
                   "HP shadow must combine draw_offset and Label shadow transforms");
            expect(std::fabs(health_draws[2U]->destination.x - 490.17) < 1e-9 &&
                       std::fabs(health_draws[2U]->destination.y - 578.7) < 1e-9 &&
                       health_draws[2U]->modulation.color ==
                           model.health_text_color(),
                   "HP duplicate foreground must follow the shadow in retail order");

            model.set_numeric_health_visible(false);
            const auto numeric_hidden = presentation.build(model, context);
            expect(!contains_text(numeric_hidden, "100") &&
                       sprite_count(numeric_hidden,
                                    "png/ui/health_bar/health_bar_frame.png") == 1U &&
                       sprite_count(numeric_hidden,
                                    "png/ui/health_bar/health_bar.png") == 1U,
                   "disabled numeric HP must preserve the frame and fill");

            model.set_class_portrait("png/ui/classes/soldier_blue.png", false);
            model.set_player_score(42, true);
            const auto score_visible = presentation.build(model, context);
            expect(sprite_count(score_visible, "png/ui/score/score_frame.png") == 1U &&
                       sprite_count(score_visible,
                                    "png/ui/classes/soldier_blue.png") == 1U,
                   "player-score authority must gate both score and portrait");
            model.set_player_score(42, false);
            const auto score_hidden = presentation.build(model, context);
            expect(sprite_count(score_hidden, "png/ui/score/score_frame.png") == 0U &&
                       sprite_count(score_hidden,
                                    "png/ui/classes/soldier_blue.png") == 0U &&
                       sprite_count(score_hidden,
                                    "png/ui/health_bar/health_bar_frame.png") == 1U,
                   "disabled player score must leave the health bar intact");
        }

        // Presentation: window-relative retail geometry at two resolutions,
        // crosshair gating, panel backing and text.
        {
            GameHudModel model;
            model.set_help_messages({"alpha", "beta"}, "[H] Close", 0.0);
            for (int tick{}; tick < 20; ++tick) {
                model.tick();
            }
            const GameHudPresentation presentation;
            // Deterministic shaped-width fake: 9 px per code unit.
            const auto measure = [](std::string_view text, double) {
                return static_cast<double>(text.size()) * 9.0;
            };

            const auto find_sprite =
                [](const battlespades::ui::DrawList& list,
                   std::string_view asset) -> const battlespades::ui::SpriteDrawCommand* {
                for (const auto& command : list.commands()) {
                    const auto* sprite = std::get_if<battlespades::ui::SpriteDrawCommand>(&command);
                    if (sprite != nullptr && sprite->asset_id == asset) {
                        return sprite;
                    }
                }
                return nullptr;
            };
            const auto find_text =
                [](const battlespades::ui::DrawList& list,
                   std::string_view text) -> const battlespades::ui::TextDrawCommand* {
                for (const auto& command : list.commands()) {
                    const auto* draw = std::get_if<battlespades::ui::TextDrawCommand>(&command);
                    if (draw != nullptr && draw->localization_key == text) {
                        return draw;
                    }
                }
                return nullptr;
            };
            const auto find_foreground_text =
                [](const battlespades::ui::DrawList& list,
                   std::string_view text,
                   battlespades::ui::ColorRgba8 rgb)
                    -> const battlespades::ui::TextDrawCommand* {
                for (const auto& command : list.commands()) {
                    const auto* draw =
                        std::get_if<battlespades::ui::TextDrawCommand>(&command);
                    if (draw != nullptr && draw->localization_key == text &&
                        draw->modulation.color.red == rgb.red &&
                        draw->modulation.color.green == rgb.green &&
                        draw->modulation.color.blue == rgb.blue) {
                        return draw;
                    }
                }
                return nullptr;
            };

            const auto bare =
                presentation.build(model, GameHudPresentationContext{{800, 600}, 1'000U, measure});
            // Retail draws no crosshair for an empty loadout, the movement
            // lessons' state.
            expect(sprite_count(bare, "png/ui/target_centre.png") == 0U,
                   "an empty loadout must draw no crosshair");

            model.set_crosshair_visible(true);
            model.set_crosshair_geometry(13.0, true);
            model.confirm_hit();
            const auto confirmed_hit =
                presentation.build(model, GameHudPresentationContext{{800, 600}, 1'000U, measure});
            const auto* hit_centre = find_sprite(confirmed_hit, "png/ui/target_centre.png");
            expect(hit_centre != nullptr && hit_centre->modulation.color ==
                                                battlespades::ui::ColorRgba8{230U, 40U, 79U, 255U},
                   "ShootResponse must tint every crosshair piece retail pink");
            for (int tick{}; tick < 16; ++tick) {
                model.tick();
            }
            const auto expired_hit =
                presentation.build(model, GameHudPresentationContext{{800, 600}, 1'000U, measure});
            const auto* normal_centre = find_sprite(expired_hit, "png/ui/target_centre.png");
            expect(normal_centre != nullptr &&
                       normal_centre->modulation.color ==
                           battlespades::ui::ColorRgba8{255U, 255U, 255U, 255U},
                   "the hit crosshair must return to white after 0.25 seconds");

            expect(battlespades::frontend::retail_score_reason_label(3U) ==
                       "Headshot" &&
                       battlespades::frontend::retail_score_reason_label(49U) ==
                           "Capture Flag" &&
                       battlespades::frontend::retail_score_reason_label(63U).empty(),
                   "score reason ordinals must use retail labels and hide profile totals");
            model.add_score_award(100, 1U);
            model.add_score_award(100, 3U);
            expect(model.score_award().displayed_delta == 100 &&
                       model.score_award().lines.size() == 3U &&
                       model.score_award().lines[2].pending_delta == 100 &&
                       model.score_award().lines[2].show_after_seconds == 0.5 &&
                       model.score_award().lines[0].ttl_seconds == 1.75,
                   "rapid awards must retain retail's delayed aggregate and TTL extension");
            const auto award_list =
                presentation.build(model, GameHudPresentationContext{{800, 600}, 1'000U, measure});
            const auto* award_value = find_foreground_text(
                award_list, "+100", {255U, 255U, 255U, 255U});
            const auto* first_reason = find_foreground_text(
                award_list, "Kill", model.team_color());
            expect(award_value != nullptr && first_reason != nullptr &&
                       !contains_text(award_list, "Headshot") &&
                       award_value->destination.x == 430.0 &&
                       award_value->destination.y == 320.0 &&
                       award_value->vertical_alignment ==
                           battlespades::ui::VerticalTextAlignment::baseline &&
                       award_value->requested_font_size_pixels == 20.0 &&
                       award_value->modulation.color.alpha == 127U &&
                       first_reason->destination.x == 430.0 &&
                       first_reason->destination.y == 352.0 &&
                       first_reason->vertical_alignment ==
                           battlespades::ui::VerticalTextAlignment::baseline &&
                       first_reason->requested_font_size_pixels == 16.0 &&
                       first_reason->modulation.color.alpha == 127U,
                   "ScoreLine must use the recovered baselines, fonts, team tint, and delay");

            for (int tick{}; tick < 31; ++tick) {
                model.tick();
            }
            expect(model.score_award().displayed_delta == 200 &&
                       model.score_award().lines[2].pending_delta == 0,
                   "HUD.update must aggregate an award only when its delayed row becomes drawable");
            const auto delayed_award_list =
                presentation.build(model, GameHudPresentationContext{{800, 600}, 1'000U, measure});
            const auto* delayed_value = find_foreground_text(
                delayed_award_list, "+200", {255U, 255U, 255U, 255U});
            const auto* delayed_reason = find_foreground_text(
                delayed_award_list, "Headshot", model.team_color());
            expect(delayed_value != nullptr && delayed_reason != nullptr &&
                       delayed_reason->destination.x == 430.0 &&
                       delayed_reason->destination.y == 379.0 &&
                       delayed_reason->vertical_alignment ==
                           battlespades::ui::VerticalTextAlignment::baseline,
                   "delayed reasons must retain their final retail row below the crosshair");

            for (int tick{}; tick < 75; ++tick) {
                model.tick();
            }
            expect(model.score_award().displayed_delta == 0 &&
                       model.score_award().lines.empty(),
                   "the score-reason stack must retire after its shared 1.75-second TTL");

            GameHudModel fading_score;
            fading_score.add_score_award(100, 1U);
            for (int tick{}; tick < 84; ++tick) {
                fading_score.tick();
            }
            const auto fading_list = presentation.build(
                fading_score,
                GameHudPresentationContext{{800, 600}, 1'000U, measure});
            const auto* fading_title = find_foreground_text(
                fading_list, "+100", {255U, 255U, 255U, 255U});
            std::size_t title_passes{};
            std::size_t black_stroke_passes{};
            std::uint8_t observed_stroke_alpha{};
            bool observed_retail_outline{};
            for (const auto& command : fading_list.commands()) {
                const auto* draw =
                    std::get_if<battlespades::ui::TextDrawCommand>(&command);
                if (draw == nullptr || draw->localization_key != "+100") {
                    continue;
                }
                ++title_passes;
                if (draw->modulation.color.red == 0U &&
                    draw->modulation.color.green == 0U &&
                    draw->modulation.color.blue == 0U) {
                    ++black_stroke_passes;
                    observed_stroke_alpha = draw->modulation.color.alpha;
                    observed_retail_outline = draw->retail_outline_stroke;
                }
            }
            expect(fading_title != nullptr &&
                       fading_title->modulation.color.alpha == 63U &&
                       !fading_title->retail_outline_stroke &&
                       title_passes == 2U && black_stroke_passes == 1U &&
                       observed_stroke_alpha == 31U &&
                       observed_retail_outline,
                   "ScoreLine fade must retain retail's foreground/stroke alpha curve");

            // A title plus four reason rows is full. Retail resets instead of
            // retaining a fifth reason and displacing an earlier causal line.
            model.add_score_award(10, 1U);
            model.add_score_award(20, 3U);
            model.add_score_award(30, 4U);
            model.add_score_award(40, 5U);
            expect(model.score_award().lines.size() == 5U,
                   "four reasons plus the title must fill the retail stack");
            model.add_score_award(50, 11U);
            expect(model.score_award().displayed_delta == 50 &&
                       model.score_award().lines.size() == 2U &&
                       model.score_award().lines[1].text == "Defend",
                   "the next score reason must start a fresh title/reason stack");
            model.set_ammo_state("png/ui/weapons/pistol.png", 7, 24, true);
            model.set_player_score(300, true);
            model.set_inventory_state({GameHudInventorySlot{"png/ui/weapons/block.png", "1"},
                                       GameHudInventorySlot{"png/ui/weapons/spade.png", "2"},
                                       GameHudInventorySlot{"png/ui/weapons/pistol.png", "3"}},
                                      1U,
                                      true);
            for (const auto& extent : {battlespades::ui::PixelExtent{800, 600},
                                       battlespades::ui::PixelExtent{1920, 1080}}) {
                const auto list =
                    presentation.build(model, GameHudPresentationContext{extent, 1'000U, measure});
                const double width = static_cast<double>(extent.width);
                const double height = static_cast<double>(extent.height);

                const auto* frame = find_sprite(list, "png/ui/health_bar/health_bar_frame.png");
                expect(frame != nullptr, "the health frame must be present");
                expect(std::fabs(frame->destination.x - (width * 0.5 - 119.5)) < 1e-9 &&
                           std::fabs(frame->destination.y - (height - 47.0)) < 1e-9 &&
                           frame->destination.width == 239.0 && frame->destination.height == 34.0,
                       "the health frame must follow the recovered window-relative rect");
                expect(frame->space == battlespades::ui::DrawSpace::window_pixels,
                       "HUD widgets must draw in live window pixels");

                const auto* centre = find_sprite(list, "png/ui/target_centre.png");
                expect(centre != nullptr, "the crosshair must be present with a tool");
                expect(std::fabs(centre->destination.x - (width * 0.5 - 8.0)) < 1e-9 &&
                           std::fabs(centre->destination.y - (height * 0.5 - 8.0)) < 1e-9 &&
                           centre->destination.width == 16.0,
                       "the crosshair must center on the window, never scaled");
                const auto* top_left = find_sprite(list, "png/ui/target_top_left.png");
                const auto* bottom_right = find_sprite(list, "png/ui/target_bottom_right.png");
                expect(
                    top_left != nullptr && bottom_right != nullptr &&
                        std::fabs(top_left->destination.x - (width * 0.5 - 8.0 - 13.0)) < 1e-9 &&
                        std::fabs(top_left->destination.y - (height * 0.5 - 8.0 - 13.0)) < 1e-9 &&
                        std::fabs(bottom_right->destination.x - (width * 0.5 - 8.0 + 13.0)) <
                            1e-9 &&
                        std::fabs(bottom_right->destination.y - (height * 0.5 - 8.0 + 13.0)) < 1e-9,
                    "the four corner sprites must move by the live accuracy radius");

                const auto* backing = find_sprite(list, "png/high/white.png");
                expect(backing != nullptr, "the help panel backing quad must be present");
                // Settled panel: backing width hugs the widest measured line
                // (9 px per glyph fake) and the top edge rests flush.
                const double biggest = 9.0 * 5.0;
                expect(std::fabs(backing->destination.width - (biggest + 40.0)) < 1e-9,
                       "the backing must hug the widest shaped line");
                expect(std::fabs(backing->destination.x - (width * 0.5 - biggest * 0.5 - 20.0)) <
                           1e-9,
                       "the backing must center on the window");
                expect(std::fabs(backing->destination.y - (30.0 - 30.166015625)) < 0.01,
                       "the settled backing must rest flush with the top edge");
                expect(contains_text(list, "alpha") && contains_text(list, "beta"),
                       "help lines must be drawn");
                expect(contains_text(list, "[H] Close"), "the close hint must be drawn");
                expect(sprite_count(list, "png/ui/weapon_select/weapon_frame_selected.png") == 1U &&
                           sprite_count(list, "png/ui/weapon_select/weapon_frame.png") == 2U,
                       "wheel HUD must draw every slot with exactly one selected frame");
                const auto* selected_frame =
                    find_sprite(list, "png/ui/weapon_select/weapon_frame_selected.png");
                expect(selected_frame != nullptr &&
                           std::fabs(selected_frame->destination.width - 136.5) < 1e-9 &&
                           std::fabs(selected_frame->destination.height - 136.5) < 1e-9,
                       "the selected frame must keep its authored retail scale");
                const auto* normal_frame =
                    find_sprite(list, "png/ui/weapon_select/weapon_frame.png");
                expect(normal_frame != nullptr &&
                           std::fabs(normal_frame->destination.width - 48.75) < 1e-9 &&
                           std::fabs(normal_frame->destination.height - 48.75) < 1e-9,
                       "normal frames must use the recovered quarter scale");
                const auto* selected_icon = find_sprite(list, "png/ui/weapons/spade.png");
                const auto* normal_icon = find_sprite(list, "png/ui/weapons/block.png");
                expect(selected_icon != nullptr && normal_icon != nullptr &&
                           std::fabs(selected_icon->destination.width - 148.5) < 1e-9 &&
                           std::fabs(normal_icon->destination.width - 66.0) < 1e-9,
                       "weapon portraits must include the retail half-scale transform");
                const auto* selected_number = find_text(list, "2");
                expect(selected_number != nullptr &&
                           std::fabs(selected_number->destination.x - (width * 0.5 + 38.0)) < 1e-9,
                       "the selected hotkey must sit at the expanded frame's lower-right");
                expect(sprite_count(list, "png/ui/weapons/block.png") == 1U &&
                           contains_text(list, "1") && contains_text(list, "3"),
                       "the tool strip must render icons and all number-key labels");
                const auto* ammo_frame = find_sprite(list, "png/ui/ammo/ammo_frame.png");
                const auto* score_frame = find_sprite(list, "png/ui/score/score_frame.png");
                expect(ammo_frame != nullptr && score_frame != nullptr,
                       "the original ammo and score frames must be restored");
                // Superseded by the hud.pyd recovery (docs/HUD_RECOVERY.md).
                // The old expectation encoded a guessed 8 px "safe edge" inset;
                // draw_tools_hud actually sets ammo_x = window.width - 80 and
                // x = ammo_x - ammo_frame.width*0.5 - 19 (= width - 156.5) with
                // the panel at y = 60 measured up from the bottom, and blits the
                // 115x40 frame under glScalef(1.3, 1.0, 0.0), so only x grows.
                expect(std::fabs(ammo_frame->destination.x - (width - 156.5)) < 1e-9 &&
                           std::fabs(ammo_frame->destination.y - (height - 100.0)) < 1e-9,
                       "the ammo frame must sit at the recovered draw_tools_hud anchor");
                expect(std::fabs(ammo_frame->destination.width - 149.5) < 1e-9 &&
                           std::fabs(ammo_frame->destination.height - 40.0) < 1e-9,
                       "the ammo frame must carry only the recovered horizontal scale");
                const battlespades::ui::SpriteDrawCommand* ammo_icon{};
                for (const auto& command : list.commands()) {
                    const auto* sprite = std::get_if<battlespades::ui::SpriteDrawCommand>(&command);
                    if (sprite != nullptr && sprite->asset_id == "png/ui/weapons/pistol.png" &&
                        std::fabs(sprite->destination.width - 33.0) < 1e-9) {
                        ammo_icon = sprite;
                        break;
                    }
                }
                expect(ammo_icon != nullptr &&
                           std::fabs(ammo_icon->destination.x - (width - 146.5)) < 1e-9,
                       "draw_ammo_hud must use its recovered 0.1 portrait scale");
                const auto* current_ammo = find_text(list, "7");
                const auto* reserve_ammo = find_text(list, "/ 24");
                expect(current_ammo != nullptr && reserve_ammo != nullptr &&
                           current_ammo->modulation.color ==
                               battlespades::frontend::hud_layout::enough_ammo_color,
                       "ammo text must preserve the measured pair and yellow threshold");
                expect(std::fabs(current_ammo->destination.x - (width - 86.5)) < 1e-9 &&
                           std::fabs(reserve_ammo->destination.x - (width - 75.5)) < 1e-9 &&
                           current_ammo->destination.y == height - 71.0 &&
                           reserve_ammo->destination.y == height - 71.0 &&
                           current_ammo->vertical_alignment ==
                               battlespades::ui::VerticalTextAlignment::baseline &&
                           reserve_ammo->vertical_alignment ==
                               battlespades::ui::VerticalTextAlignment::baseline,
                       "ammo and reserve must share retail's exact y+11 baseline");
                // Also superseded: the box is not flush in the corner. HUD.draw
                // calls draw_player_score(MSG_LEFT_MARGIN, window.height - 48),
                // and MSG_LEFT_MARGIN is 12 (hud.pyd inithud 0x100c5920). The
                // frame is bottom-left anchored (the only HUD frame loaded
                // center=False), so its top edge lands 48 - 40 = 8 px down.
                expect(score_frame->destination.x == 12.0 && score_frame->destination.y == 8.0 &&
                           score_frame->destination.width == 200.0 &&
                           score_frame->destination.height == 40.0,
                       "the score frame must use the recovered top-left retail anchor");
                // draw_player_score builds strings.SCORE + ': ' + str(int(score))
                // (hud.pyd 0x100932a1 / 0x100932cb / 0x10093303). The colon is
                // part of the retail string and is visible in the reference
                // screenshots as "SCORE: 100".
                expect(contains_text(list, "7") && contains_text(list, "/ 24") &&
                           contains_text(list, "SCORE: 300"),
                       "ammo, reserve and score values must be rendered");
                const auto* score_text = find_text(list, "SCORE: 300");
                expect(score_text != nullptr &&
                           score_text->destination == battlespades::ui::DrawRect{
                               22.0, 8.0, 200.0, 40.0} &&
                           score_text->vertical_alignment ==
                               battlespades::ui::VerticalTextAlignment::retail_center &&
                           score_text->fit == battlespades::ui::TextFit::retail_width_scale,
                       "personal score must use retail metric centering inside its frame");
            }

            model.set_crosshair_geometry(6.0, false);
            const auto no_center =
                presentation.build(model, GameHudPresentationContext{{800, 600}, 1'000U, measure});
            expect(sprite_count(no_center, "png/ui/target_centre.png") == 0U &&
                       sprite_count(no_center, "png/ui/target_top_left.png") == 1U,
                   "show_crosshair_centre must gate only the centre sprite");

            model.set_block_state("png/ui/weapons/block.png", 200, 1000, true);
            model.set_team_color(battlespades::ui::ColorRgba8{12U, 34U, 56U, 255U});
            const auto blocks_list =
                presentation.build(model, GameHudPresentationContext{{800, 600}, 1'000U, measure});
            const battlespades::ui::SpriteDrawCommand* block_panel_icon{};
            for (const auto& command : blocks_list.commands()) {
                const auto* sprite = std::get_if<battlespades::ui::SpriteDrawCommand>(&command);
                if (sprite != nullptr && sprite->asset_id == "png/ui/weapons/block.png" &&
                    std::fabs(sprite->destination.width - 33.0) < 1e-9) {
                    block_panel_icon = sprite;
                    break;
                }
            }
            expect(block_panel_icon != nullptr &&
                       block_panel_icon->modulation.color ==
                           battlespades::ui::ColorRgba8{12U, 34U, 56U, 255U},
                   "the block portrait must be 33px and tinted by live team colour");
            expect(contains_text(blocks_list, "/ 1000"),
                   "block reserve must preserve retail slash-space formatting");

            model.set_prefab_cost_state(
                "prefabs/prefab_barricade.png", 75, false,
                battlespades::ui::ColorRgba8{12U, 34U, 56U, 255U}, true);
            const auto prefab_list = presentation.build(
                model, GameHudPresentationContext{{800, 600}, 1'000U, measure});
            const auto* prefab_icon =
                find_sprite(prefab_list, "prefabs/prefab_barricade.png");
            const auto* prefab_cost = find_text(prefab_list, "75");
            expect(prefab_icon != nullptr &&
                       std::fabs(prefab_icon->destination.width - 82.5) < 1e-9 &&
                       std::fabs(prefab_icon->destination.height - 82.5) < 1e-9,
                   "prefab cost icon must use retail draw_ammo_hud scale 0.25");
            expect(prefab_icon != nullptr &&
                       prefab_icon->modulation.color ==
                           battlespades::ui::ColorRgba8{12U, 34U, 56U, 255U},
                   "prefab cost icon must inherit the owning team tint");
            expect(prefab_cost != nullptr &&
                       prefab_cost->modulation.color ==
                           battlespades::frontend::hud_layout::not_enough_ammo_color,
                   "unaffordable prefab cost must use retail's red threshold colour");
            expect(model.blocks().visible,
                   "selecting a prefab must not hide the independent block-stock row");

            model.set_respawn_time(5U);
            const auto death_camera_list =
                presentation.build(model, GameHudPresentationContext{{800, 600}, 1'000U, measure});
            expect(contains_text(death_camera_list, "Respawning in 5") &&
                       !contains_text(death_camera_list, "DEATH CAMERA") &&
                       !contains_text(death_camera_list, "SPECTATING") &&
                       !contains_text(death_camera_list,
                                      "MOVE MOUSE OR CLICK TO CHASE") &&
                       !contains_text(death_camera_list,
                                      "MOUSE WHEEL TO CHANGE PLAYER"),
                   "death HUD must retain the retail respawn label without fabricated camera text");
            for (const auto& command : death_camera_list.commands()) {
                if (const auto* text = std::get_if<battlespades::ui::TextDrawCommand>(&command)) {
                    expect(text->localization_key.find('\n') == std::string::npos,
                           "HUD text must be split before single-line rasterization");
                }
            }
            model.clear_respawn();

            // A map transition must replace the runtime texture identity. The
            // GPU cache may still contain the prior map for a frame, so the HUD
            // must never keep issuing draw commands for that old identity.
            battlespades::frontend::GameHudMinimapState first_map;
            first_map.texture_asset = "runtime/minimap/41";
            first_map.visible = true;
            first_map.focus_x = 256.0;
            first_map.focus_y = 256.0;
            model.set_minimap(first_map);
            const auto first_minimap =
                presentation.build(model, GameHudPresentationContext{{800, 600}, 1'000U, measure});
            expect(sprite_count(first_minimap, "runtime/minimap/41") == 1U,
                   "the active map must draw its generation-specific minimap texture");

            auto second_map = first_map;
            second_map.texture_asset = "runtime/minimap/42";
            model.set_minimap(std::move(second_map));
            const auto second_minimap =
                presentation.build(model, GameHudPresentationContext{{800, 600}, 1'000U, measure});
            expect(sprite_count(second_minimap, "runtime/minimap/42") == 1U &&
                       sprite_count(second_minimap, "runtime/minimap/41") == 0U,
                   "map rotation must stop referencing the previous minimap preview");

            model.set_inventory_state(model.inventory_slots(), 2U, false);
            const auto direct_hidden =
                presentation.build(model, GameHudPresentationContext{{800, 600}, 1'000U, measure});
            expect(sprite_count(direct_hidden, "png/ui/weapon_select/weapon_frame_selected.png") ==
                       0U,
                   "direct number selection must not open the wheel toolbar");

            // The complete developer catalog includes selectable tools whose
            // retail definition has no icon. Opening the wheel must retain a
            // frame for such a slot without emitting an invalid empty asset.
            model.set_inventory_state({GameHudInventorySlot{"", ""},
                                       GameHudInventorySlot{"png/ui/weapons/pistol.png", "2"}},
                                      0U,
                                      true);
            const auto iconless =
                presentation.build(model, GameHudPresentationContext{{800, 600}, 1'000U, measure});
            for (const auto& command : iconless.commands()) {
                if (const auto* sprite =
                        std::get_if<battlespades::ui::SpriteDrawCommand>(&command)) {
                    expect(!sprite->asset_id.empty(),
                           "iconless inventory slots must not emit empty sprites");
                }
            }

            std::vector<GameHudInventorySlot> complete_catalog;
            complete_catalog.reserve(battlespades::world::weapon_catalog().size());
            for (const auto& weapon : battlespades::world::weapon_catalog()) {
                if (!weapon.toolbar_icon_asset.empty()) {
                    expect(weapon.toolbar_icon_asset.starts_with("png/ui/weapons/"),
                           "every retail tool strip image must come from TOOL_IMAGES");
                }
                complete_catalog.push_back({std::string{weapon.toolbar_icon_asset}, std::string{}});
            }
            model.set_inventory_state(std::move(complete_catalog), 8U, true);
            const auto full_wheel = presentation.build(
                model, GameHudPresentationContext{{1680, 1050}, 1'000U, measure});
            for (const auto& command : full_wheel.commands()) {
                if (const auto* sprite =
                        std::get_if<battlespades::ui::SpriteDrawCommand>(&command)) {
                    expect(!sprite->asset_id.empty(),
                           "the 65-tool wheel must never reach the renderer with an empty asset");
                }
            }

            model.toggle_hud();
            const auto hidden =
                presentation.build(model, GameHudPresentationContext{{800, 600}, 1'000U, measure});
            expect(sprite_count(hidden, "png/ui/health_bar/health_bar.png") == 0U,
                   "toggling the HUD must hide world-anchored widgets");
            expect(sprite_count(hidden, "png/high/white.png") == 1U,
                   "the help panel must survive the HUD toggle");
        }

        // Lesson driver: recovered thresholds and monotonic progression.
        {
            GameHudModel model;
            const GameHudPresentation presentation;
            const auto without =
                presentation.build(model, GameHudPresentationContext{{800, 600}, 1'000U, {}});
            const auto palette = retail_block_palette();
            model.set_palette_state(
                std::vector<battlespades::ui::ColorRgba8>{palette.begin(), palette.end()},
                8U,
                10U,
                true);
            const auto list =
                presentation.build(model, GameHudPresentationContext{{800, 600}, 1'000U, {}});
            expect(model.palette().visible && model.palette().selected == 10U &&
                       model.palette().colors.size() == 32U,
                   "the enabled block palette must retain all selectable swatches");
            expect(sprite_count(list, "png/high/white.png") ==
                       sprite_count(without, "png/high/white.png") + 33U,
                   "the palette must draw 32 cells and one selected-cell border");
        }

        // Lesson driver: recovered thresholds and monotonic progression.
        {
            GameHudModel model;
            model.set_inside_zone_tint(
                battlespades::ui::ColorRgba8{44U, 117U, 179U, 150U});
            const GameHudPresentation presentation;
            const auto list = presentation.build(
                model, GameHudPresentationContext{{1280, 720}, 1'000U, {}});
            const auto* tint = std::get_if<battlespades::ui::SpriteDrawCommand>(
                &list.commands().front());
            expect(tint != nullptr &&
                       tint->asset_id == "png/high/alpha_block_inside_zone.png" &&
                       tint->destination == battlespades::ui::DrawRect{0.0, 0.0, 1280.0, 720.0} &&
                       tint->modulation.color ==
                           battlespades::ui::ColorRgba8{44U, 117U, 179U, 150U},
                   "inside-zone tint must cover the viewport behind normal HUD widgets");
        }

        // Lesson driver: recovered thresholds and monotonic progression.
        {
            TutorialLessons lessons;
            expect(lessons.stage() == TutorialLessonStage::intro, "lessons start at INTRO");
            bool changed{};
            for (int tick{}; tick < 181; ++tick) {
                changed = lessons.tick(fixed_dt, 140.5, false, false) || changed;
            }
            expect(changed && lessons.stage() == TutorialLessonStage::basic_controls,
                   "INTRO must advance after the recovered 3 seconds");

            expect(!lessons.tick(fixed_dt, 136.0, false, false),
                   "x above the gate must not advance BASIC_CONTROLS");
            expect(lessons.tick(fixed_dt, 134.9, false, false) &&
                       lessons.stage() == TutorialLessonStage::jump,
                   "reaching x<=135 must enter JUMP");

            expect(!lessons.tick(fixed_dt, 127.0, false, false),
                   "reaching x<=128 without a jump must not advance");
            expect(lessons.tick(fixed_dt, 127.0, true, false) &&
                       lessons.stage() == TutorialLessonStage::crouch,
                   "a seen jump at x<=128 must enter CROUCH");

            expect(!lessons.tick(fixed_dt, 100.0, false, false),
                   "x above the crouch fallback must not advance without crouch");
            expect(lessons.tick(fixed_dt, 98.9, false, false) &&
                       lessons.stage() == TutorialLessonStage::shooting,
                   "the x<=99 geometry fallback must enter SHOOTING");

            expect(!lessons.tick(fixed_dt, 10.0, true, true),
                   "SHOOTING must wait for the external target gate");
            expect(lessons.advance_external(TutorialLessonStage::shooting) &&
                       lessons.stage() == TutorialLessonStage::climb,
                   "destroying the targets must enter CLIMB");
            expect(lessons.advance_external(TutorialLessonStage::climb) &&
                       lessons.stage() == TutorialLessonStage::complete,
                   "building must enter COMPLETE");

            const auto keys = TutorialLessons::message_keys(TutorialLessonStage::basic_controls);
            expect(keys.size() == 3U && keys[0U] == "TUTORIAL_BASIC_CONTROLS_1",
                   "message keys must match the recovered HELP_BY_STAGE table");
        }

        // The jump gate's x<=119 fallback advances without a seen jump.
        {
            TutorialLessons lessons;
            for (int tick{}; tick < 181; ++tick) {
                static_cast<void>(lessons.tick(fixed_dt, 140.0, false, false));
            }
            static_cast<void>(lessons.tick(fixed_dt, 134.0, false, false));
            expect(lessons.stage() == TutorialLessonStage::jump, "must be at JUMP");
            expect(lessons.tick(fixed_dt, 118.9, false, false) &&
                       lessons.stage() == TutorialLessonStage::crouch,
                   "crossing x<=119 must prove the ledge without a jump sample");
        }

        std::cout << "game hud: retail help panel, crosshair and lesson checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
