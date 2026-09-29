#include "battlespades/frontend/loading_presentation.hpp"

#include <exception>
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {
using namespace battlespades::frontend;
using namespace battlespades::ui;

void expect(bool value, const char* message) {
    if (!value) throw std::runtime_error{message};
}

const TextDrawCommand* find_text(const DrawList& list, std::string_view key) {
    for (const auto& command : list.commands()) {
        if (const auto* text = std::get_if<TextDrawCommand>(&command);
            text && text->localization_key == key) return text;
    }
    return nullptr;
}

void tabs_and_art_stay_in_the_loading_frame() {
    MatchLoadingModel model;
    model.begin("London", "zom");
    expect(model.snapshot().tabs.size() == 3U,
           "hosting with known metadata should expose the three loading tabs immediately");
    for (const auto window : std::array<PixelExtent, 4U>{{{800, 600}, {1000, 750}, {1280, 720}, {3440, 1440}}}) {
        for (std::size_t selected{}; selected < 3U; ++selected) {
            expect(model.select_tab(selected), "all loading tabs should be selectable");
            const auto draw = MatchLoadingPresentation{}.build(model.snapshot(), {window});
            for (std::size_t index{}; index < 3U; ++index) {
                const auto bounds = loading_layout::tab(index);
                expect(bounds.x >= loading_layout::content.x &&
                    bounds.x + bounds.width <= loading_layout::content.x + loading_layout::content.width,
                    "tabs must fit the content frame instead of spilling left");
                const auto* label = find_text(draw, std::array{"MAP", "MODE", "SCORES"}[index]);
                expect(label && label->destination.x == bounds.x - 5.5 &&
                           label->destination.y == bounds.y - 3.5 &&
                           label->destination.width == bounds.width &&
                           label->destination.height == bounds.height,
                       "tab labels sit at retail's measured offset from the hit target");
            }
            for (const auto& command : draw.commands()) {
                if (const auto* image = std::get_if<SpriteDrawCommand>(&command);
                    image && (image->asset_id.find("map_images/") != std::string::npos ||
                              image->asset_id.find("mode_infographics/") != std::string::npos)) {
                    expect(image->destination.x >= loading_layout::content.x &&
                        image->destination.y >= loading_layout::content.y &&
                        image->destination.x + image->destination.width <= 740.48 &&
                        image->destination.y + image->destination.height <= 439.02,
                        "map and infographic artwork must stay above Start/progress at every aspect ratio");
                }
            }
        }
    }
}

void mode_captions_and_scores_are_visible_and_interactive() {
    MatchLoadingModel model;
    model.initial_info("London", "ZOMBIE_MODE_TITLE", false, {}, false, true);
    expect(model.select_tab(1U), "Mode tab should exist");
    auto draw = MatchLoadingPresentation{}.build(model.snapshot());
    for (const auto key : {"ZOM_INFOGRAPHIC_TEXT1", "ZOM_INFOGRAPHIC_TEXT2", "ZOM_INFOGRAPHIC_TEXT3"}) {
        const auto* caption = find_text(draw, key);
        expect(caption && caption->layout == TextLayout::retail_wrapped_lines &&
            caption->requested_font_size_pixels == 20.0 &&
            caption->destination.width == 177.0 && caption->destination.height == 35.0 &&
            caption->destination.y + caption->destination.height < 439.0,
            "captions must use retail's 180x35 shrink-to-fit Spades 20 layout on their plates");
    }
    // loadingMenu.draw_infographic_tab: mode_text (the upper-cased mode title)
    // over the illustration, with drop shadows and an outline under the fill.
    std::size_t title_layers{};
    bool outlined{};
    for (const auto& command : draw.commands()) {
        if (const auto* text = std::get_if<TextDrawCommand>(&command);
            text && text->localization_key == "ZOMBIE_MODE_TITLE") {
            ++title_layers;
            outlined = outlined || text->retail_outline_stroke;
            expect(text->transform == TextTransform::uppercase &&
                       text->requested_font_size_pixels == 38.0,
                   "mode title must be upper-cased Spades 38");
        }
    }
    expect(title_layers == 4U && outlined,
           "mode title must draw two shadows, an outline and the fill");
    expect(model.select_tab(2U), "Scores tab should exist");
    draw = MatchLoadingPresentation{}.build(model.snapshot());
    expect(find_text(draw, "MODE_SPECIFIC_SCORE_TYPES") && find_text(draw, "GENERIC_SCORE_TYPES") &&
        find_text(draw, "Survive") && find_text(draw, "+50 every 10 seconds") &&
        find_text(draw, "Last Man Standing") && find_text(draw, "+150 every 5 seconds") &&
        find_text(draw, "Headshot"), "Scores must show real mode and generic scoring rules");
    expect(model.scroll_scores(100), "long scoring lists must scroll");
    expect(model.snapshot().score_scroll == 6U, "scroll must stop at the last complete page");
    draw = MatchLoadingPresentation{}.build(model.snapshot());
    expect(find_text(draw, "Team Kill") != nullptr,
           "friendly fire should expose the team-kill penalty at the bottom of the list");
    expect(model.set_score_scroll(0.0), "scrollbar must return to the first page");
    const auto thumb_top = loading_layout::score_thumb(model.snapshot());
    expect(thumb_top.y == loading_layout::score_track.y && thumb_top.height < loading_layout::score_track.height,
           "scroll thumb must start at the track edge and reflect the visible fraction");
    expect(model.handle_score_click(thumb_top.x + 5.0, thumb_top.y + thumb_top.height * 0.5) &&
        model.snapshot().score_scroll == 0U, "grabbing the thumb must not jump the scroll offset");
    expect(model.handle_score_click(thumb_top.x + 5.0,
        loading_layout::score_track.y + loading_layout::score_track.height - 1.0),
        "track below the thumb must page down");
    const auto thumb_bottom = loading_layout::score_thumb(model.snapshot());
    expect(std::abs(thumb_bottom.y + thumb_bottom.height -
        loading_layout::score_track.y - loading_layout::score_track.height) < 0.001,
        "last-page thumb must end exactly at the track bottom");
    expect(model.set_score_scroll(0.0), "reset scrollbar before category interaction");
    expect(model.handle_score_click(680.0, 160.0), "mode category should collapse");
    expect(model.snapshot().score_rows.size() == 12U, "collapsed mode keeps generic rows available");
    expect(model.handle_score_click(680.0, 187.0), "generic category should collapse independently");
    expect(model.snapshot().score_rows.size() == 2U && model.snapshot().score_scroll == 0U,
           "collapsing both categories must leave two headings and a valid scrollbar");
    expect(!model.handle_score_click(60.0, 150.0), "clicks outside the score list must not toggle categories");
    model.begin("London", "TDM_TITLE");
    expect(model.select_tab(2U), "new session retains scores tab");
    expect(model.snapshot().score_rows.size() == 12U,
           "new sessions must reset collapsed categories and friendly-fire state");
}

void authoritative_captions_and_hosting_status_do_not_fake_readiness() {
    MatchLoadingModel model;
    model.begin("London", "TDM");
    model.set_infographic_captions({"SERVER_CUSTOM_CAPTION", {}, std::string(600U, 'x')});
    const auto snapshot = model.snapshot();
    expect(snapshot.infographic_captions[0] == "SERVER_CUSTOM_CAPTION" &&
        snapshot.infographic_captions[1] == "TDM_INFOGRAPHIC_TEXT2" &&
        snapshot.infographic_captions[2].size() == 256U,
        "authoritative captions must override stock text with bounded, nonempty values");
    model.set_infographic_captions({"OC_INFOGRAPHIC_TEXT1", "CCTF_INFOGRAPHIC_TEXT2", "NOR_INFOGRAPHIC_TEXT3"});
    expect(model.snapshot().infographic_captions == std::array<std::string, 3U>{
        "OCC_INFOGRAPHIC_TEXT1", "CTF_INFOGRAPHIC_TEXT2", "TDM_INFOGRAPHIC_TEXT3"},
        "server short mode codes must resolve to existing retail localization keys");
    model.set_status("Starting local server...");
    const auto draw = MatchLoadingPresentation{}.build(model.snapshot());
    const auto* status = find_text(draw, "Starting local server...");
    expect(status && status->requested_font_size_pixels >= 13.0 &&
        status->destination.y + status->destination.height <= 580.0,
        "hosting status must remain readable inside the loading footer");
    expect(!model.snapshot().start_enabled && model.snapshot().overall_progress == 0.0,
        "host status wording must never enable Start before real readiness");
}

void every_stock_mode_resolves_its_own_reference_content() {
    constexpr std::array codes{"tdm", "ctf", "cctf", "dem", "dia", "oc", "vip", "zom", "tc", "mh"};
    constexpr std::array first_rows{"Distraction", "Carry Flag", "Carry Flag", "Destroy Base",
        "Carry diamond", "Occupy", "VIP Survive", "Survive", "Controlled Territories", "Occupy"};
    constexpr std::array prefixes{"TDM", "CTF", "CTF", "DEM", "DIA", "OCC", "VIP", "ZOM", "TC", "MH"};
    MatchLoadingModel model;
    for (std::size_t index{}; index < codes.size(); ++index) {
        model.begin("London", codes[index]);
        const auto snapshot = model.snapshot();
        expect(snapshot.score_rows.size() > 10U && snapshot.score_rows[1].label_key == first_rows[index],
            "each stock mode needs its own verified scoring rules");
        expect(snapshot.infographic_captions[0] == std::string{prefixes[index]} + "_INFOGRAPHIC_TEXT1",
            "short mode codes must select the correct caption set");
    }
    model.begin("My custom map", "UGC");
    expect(model.snapshot().tabs.size() == 2U &&
        model.snapshot().textures.infographic_asset.find("infographic_ugc") != std::string::npos,
        "map creator should have its own infographic without a combat Scores tab");
    model.begin("Training", "TUTORIAL");
    expect(model.snapshot().tabs.size() == 1U, "Training should retain its single Map tab");
}

const SpriteDrawCommand* find_sprite(const DrawList& list, std::string_view asset) {
    for (const auto& command : list.commands()) {
        if (const auto* sprite = std::get_if<SpriteDrawCommand>(&command);
            sprite && sprite->asset_id == asset) return sprite;
    }
    return nullptr;
}

void status_lines_are_localised_with_the_map_name() {
    const auto localize = [](std::string_view key) -> std::string {
        if (key == "CHECKING_MAP") return "Karte {0} wird gepruft";
        if (key == "RECEIVING_SERVER_PACKS") return "Connected, receiving server packs...";
        if (key == "ERROR_TIMEOUT") return "Zeitlimit";
        return std::string{key};
    };
    MatchLoadingModel model;
    model.begin("Hiesville", "TDM_TITLE");
    model.receiving_packs();
    expect(loading_status_text(model.snapshot(), localize) ==
               "LITERAL|Connected, receiving server packs...",
           "RECEIVING_SERVER_PACKS must come from the catalogue");
    model.initial_info("Hiesville", "TDM_TITLE", false, "");
    const auto checking = loading_status_text(model.snapshot(), localize);
    expect(checking == "LITERAL|Karte Hiesville wird gepruft",
           "map stages must fill {0} with the map name in the active language");
    model.set_status("Starting local server...");
    expect(loading_status_text(model.snapshot(), localize) == "Starting local server...",
           "host stages the caller worded are drawn verbatim");
    model.begin("Hiesville", "TDM_TITLE");
    model.tick(30.5);
    expect(model.snapshot().state == MatchLoadingState::timed_out &&
               loading_status_text(model.snapshot(), localize) == "LITERAL|Zeitlimit",
           "the timeout reads ERROR_TIMEOUT");
}

void map_tab_draws_preview_tagline_and_custom_rules() {
    MatchLoadingModel model;
    model.begin("Trenches", "TDM_TITLE");
    model.initial_info("Trenches", "TDM_TITLE", false, "");
    model.set_custom_game_rules({{"RULE_ENABLE_BLOCKS", "OFF"},
                                 {"RULE_TDM_SCORE_TARGET", "100"},
                                 {"RULE_ONE_HIT_KILL", "ON"}});
    const auto snapshot = model.snapshot();
    expect(snapshot.map_tagline_key == "Trenches_TagLine", "MAP_NAME_TAGLINES selects a tagline");
    expect(snapshot.map_preview_asset.find("Trenches.png") != std::string::npos,
           "the MAP tab reuses the browser's map preview");
    expect(snapshot.custom_rules.size() == 5U && snapshot.custom_rules[0].category &&
               snapshot.custom_rules[0].label_key == "GENERAL" &&
               snapshot.custom_rules[1].label_key == "RULE_ENABLE_BLOCKS" &&
               snapshot.custom_rules[1].value == "LITERAL|OFF" &&
               snapshot.custom_rules[3].category &&
               snapshot.custom_rules[3].label_key == "TDM_TITLE" &&
               !snapshot.custom_rules[3].uppercase,
           "rules group under sorted categories, mode buckets by their title");
    const auto draw = MatchLoadingPresentation{}.build(snapshot);
    const auto* frame = find_sprite(draw, "png/ui/game_loading/minimap_bg.png");
    expect(frame && frame->destination.width == 258.0 && frame->destination.x == 459.0,
           "loading_map_frame is 258x258 at the retail position");
    const auto* preview = find_sprite(draw, snapshot.map_preview_asset);
    expect(preview && preview->destination.width == 238.0, "the preview is drawn at 238x238");
    const auto* tagline = find_text(draw, "Trenches_TagLine");
    expect(tagline && tagline->requested_font_size_pixels == 20.0, "the tagline is Spades 20");
    const auto* title = find_text(draw, "Trenches");
    expect(title && title->requested_font_size_pixels == 38.0,
           "a tagline switches the title to the smaller font");
    expect(find_text(draw, "CUSTOM_GAME_RULES") != nullptr, "the rules panel has its header");

    model.set_custom_game_rules({});
    expect(model.snapshot().custom_rules.empty(), "no rules means no panel");
    model.begin("Training", "TUTORIAL_MODE_TITLE");
    model.initial_info("Training", "TUTORIAL_MODE_TITLE", false, "");
    model.set_custom_game_rules({{"RULE_ENABLE_BLOCKS", "OFF"}});
    expect(model.snapshot().custom_rules.empty(), "the tutorial never lists rules");
    const auto tutorial = MatchLoadingPresentation{}.build(model.snapshot());
    expect(find_text(tutorial, "TUTORIAL_MODE_TITLE") != nullptr,
           "Training's title is TUTORIAL_MODE_TITLE");
}

void tabs_keep_cycling_until_start_and_input_stops_them() {
    MatchLoadingModel model;
    model.begin("London", "TDM_TITLE");
    model.initial_info("London", "TDM_TITLE", false, "");
    battlespades::assets::PreloadSnapshot assets;
    assets.state = battlespades::assets::PreloadBatchState::ready;
    assets.progress = 1.0;
    model.set_preload_snapshot(assets);
    expect(model.snapshot().start_enabled, "precondition: the map is ready");
    const auto before = model.snapshot().selected_tab;
    model.tick(3.1);
    expect(model.snapshot().selected_tab != before, "tabs keep cycling once the map is ready");
    model.tick(60.0);
    expect(model.snapshot().state == MatchLoadingState::ready,
           "a ready loader never times out while waiting for START");
    model.interrupt_tab_cycle();
    const auto held = model.snapshot().selected_tab;
    model.tick(9.0);
    expect(model.snapshot().selected_tab == held, "any input stops the cycle");
}
} // namespace

int main() {
    try {
        battlespades::frontend::BootLoadingPresentation boot_presentation;
        battlespades::frontend::MatchLoadingPresentation match_presentation;
        const battlespades::frontend::BootLoadingSnapshot boot{
            18U, 0.5, battlespades::assets::PreloadBatchState::active};
        const auto boot_list = boot_presentation.build(boot);
        if (boot_list.size() != 56U) {
            throw std::runtime_error{
                "boot screen must draw two layers, 36 dark bullets, and 18 filled bullets"};
        }

        battlespades::frontend::MatchLoadingModel model;
        model.begin("Atlantis", "CTF", false, "classic");
        model.initial_info("Atlantis", "CTF", false, "classic");
        model.receiving_map();
        model.map_progress(0.5);
        const auto snapshot = model.snapshot();
        const auto match_list = match_presentation.build(snapshot);
        if (match_list.empty() || snapshot.textures.map_image_asset.find("atlantis") ==
                                      std::string::npos) {
            throw std::runtime_error{"match screen must resolve and draw its map artwork"};
        }
        tabs_and_art_stay_in_the_loading_frame();
        mode_captions_and_scores_are_visible_and_interactive();
        authoritative_captions_and_hosting_status_do_not_fake_readiness();
        every_stock_mode_resolves_its_own_reference_content();
        status_lines_are_localised_with_the_map_name();
        map_tab_draws_preview_tagline_and_custom_rules();
        tabs_keep_cycling_until_start_and_input_stops_them();
        std::cout << "9/9 tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "[FAIL] " << error.what() << '\n';
        return 1;
    }
}
