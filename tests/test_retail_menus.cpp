// Retail in-game menu parity: SelectClass scrollbar maths, absolute class
// keys, server class data (disabled tools, constructs, locked classes),
// popups, KickVotePlayerSelect and the Controls key names.

#include "battlespades/frontend/class_selection_menu.hpp"
#include "battlespades/frontend/class_selection_presentation.hpp"
#include "battlespades/frontend/kick_vote_menu.hpp"
#include "battlespades/frontend/settings_menu.hpp"
#include "battlespades/settings/client_settings.hpp"
#include "battlespades/world/class_catalog.hpp"
#include "battlespades/world/class_selection.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>

namespace {

using battlespades::frontend::ClassSelectionMenuModel;
using battlespades::frontend::ClassSelectionPresentation;
using battlespades::ui::DrawList;
using battlespades::ui::SpriteDrawCommand;
using battlespades::ui::TextDrawCommand;

void expect(bool condition, const char* message) {
    if (!condition) throw std::runtime_error{message};
}

[[nodiscard]] bool near(double left, double right) {
    return std::abs(left - right) < 1e-6;
}

[[nodiscard]] const SpriteDrawCommand* find_sprite(const DrawList& draw, std::string_view asset) {
    for (const auto& command : draw.commands()) {
        const auto* sprite = std::get_if<SpriteDrawCommand>(&command);
        if (sprite != nullptr && sprite->asset_id == asset) return sprite;
    }
    return nullptr;
}

[[nodiscard]] bool has_text(const DrawList& draw, std::string_view key) {
    return std::ranges::any_of(draw.commands(), [key](const auto& command) {
        const auto* text = std::get_if<TextDrawCommand>(&command);
        return text != nullptr && text->localization_key == key;
    });
}

constexpr std::array<std::uint8_t, 7U> seven{0U, 1U, 2U, 3U, 12U, 16U, 17U};

void scrollbar_maths() {
    using battlespades::frontend::retail_class_thumb_length;
    using battlespades::frontend::retail_drag_scroll_index;
    expect(retail_class_thumb_length(6U) == 510.0 && retail_class_thumb_length(7U) == 437.0 &&
               retail_class_thumb_length(8U) == 382.0 && retail_class_thumb_length(10U) == 306.0,
           "thumb length is floor(612 * 5 / N)");
    expect(retail_class_thumb_length(5U) == 612.0, "five classes fill the channel");
    expect(retail_drag_scroll_index(0.0, 2U) == 0U && retail_drag_scroll_index(0.4, 2U) == 1U &&
               retail_drag_scroll_index(1.9, 2U) == 1U && retail_drag_scroll_index(2.0, 2U) == 2U,
           "set_as_int=False maps any drag past zero to index 1 (retail quirk)");

    ClassSelectionMenuModel menu;
    menu.configure(seven, 2U, 0U);
    expect(menu.has_scrollbar(), "seven classes need the HorizontalScrollBar");
    auto geometry = menu.scrollbar_geometry();
    expect(geometry.frame == battlespades::ui::Rect{70, 249, 660, 22} &&
               geometry.channel == battlespades::ui::Rect{94, 250, 612, 20},
           "black 660x22 frame and 612x20 channel");
    expect(near(geometry.thumb_x, 93.0) && near(geometry.thumb_length, 437.0) &&
               near(geometry.coord_at_max, 268.0),
           "bar_coord_at_min 93, max 93 + 612 - L");
    expect(!geometry.dec_enabled && geometry.inc_enabled, "arrows disable at the ends");

    // Arrow click: one class per release; a disabled arrow does nothing.
    static_cast<void>(menu.click({80, 260}));
    expect(menu.visible_class_offset() == 0U, "disabled dec arrow is inert");
    static_cast<void>(menu.click({719, 260}));
    expect(menu.visible_class_offset() == 1U && near(menu.scrollbar_geometry().thumb_x, 180.5),
           "inc arrow steps one class; thumb at 93 + 1 * 175 / 2");
    expect(menu.take_audio_cue() == battlespades::frontend::ClassSelectionAudioCue::scroll,
           "scrolling plays menu_scrollA");
    expect(menu.selected_class_index() == 0U, "scrolling never changes the selected class");

    // Thumb drag: the thumb centre follows the cursor, the list steps by index.
    menu.configure(seven, 2U, 0U);
    menu.pointer_press(battlespades::ui::Point{100, 260});
    expect(menu.scrollbar_dragging(), "a press inside the thumb starts the drag");
    menu.pointer_move(battlespades::ui::Point{400, 262});
    expect(menu.visible_class_offset() == 1U, "dragging past zero reaches index 1");
    expect(near(menu.scrollbar_geometry().thumb_x, 181.0), "thumb drawn at floor(x - L/2)");
    menu.pointer_move(battlespades::ui::Point{790, 262});
    expect(menu.visible_class_offset() == 2U && near(menu.scrollbar_geometry().thumb_x, 268.0),
           "the thumb clamps at bar_coord_at_max");
    expect(!menu.pointer_release(battlespades::ui::Point{790, 262}).has_value() &&
               !menu.scrollbar_dragging(),
           "releasing a drag ends it without activating anything");

    // Track click jumps the thumb centre to the cursor.
    menu.configure(seven, 2U, 0U);
    static_cast<void>(menu.click({700, 260}));
    expect(menu.visible_class_offset() == 2U, "a release on the channel jumps the thumb");

    // Wheel (native convenience) moves the viewport like an arrow.
    menu.scroll_classes(-1);
    expect(menu.visible_class_offset() == 1U, "wheel steps one card");

    ClassSelectionMenuModel five;
    constexpr std::array<std::uint8_t, 5U> five_classes{0U, 1U, 2U, 3U, 12U};
    five.configure(five_classes, 2U, 0U);
    expect(!five.has_scrollbar() && five.classes_per_page() == 5U,
           "exactly five classes use the five-card strip with no scrollbar");

    const auto draw = ClassSelectionPresentation{}.build(menu, {800, 600});
    expect(find_sprite(draw, "png/ui/common_elements/scroll_bar/scrollbar_square_button_default.png") ==
               nullptr,
           "the thumb is the 3-slice gold bar, not a stretched square button");
    const auto* mid = find_sprite(draw, "png/ui/common_elements/scroll_bar/scrollbar_hmid.png");
    expect(mid != nullptr && near(mid->destination.height, 20.0) && near(mid->destination.y, 250.0),
           "thumb body is 20 px tall inside the channel");
    bool channel{};
    for (const auto& command : draw.commands()) {
        const auto* sprite = std::get_if<SpriteDrawCommand>(&command);
        if (sprite != nullptr && sprite->asset_id == "png/high/white.png" &&
            sprite->modulation.color == battlespades::ui::ColorRgba8{73U, 63U, 7U, 255U} &&
            near(sprite->destination.width, 612.0)) {
            channel = true;
        }
    }
    expect(channel, "the channel is the dark (73,63,7) quad, not the thumb texture");
    expect(find_sprite(draw, "png/ui/common_elements/buttons/button_square.png") != nullptr,
           "arrow buttons draw the square button body");
}

void absolute_class_keys() {
    ClassSelectionMenuModel menu;
    menu.configure(seven, 2U, 0U);
    menu.class_key_press(6U);
    expect(menu.held_class_key() == 6U, "a held key shows key_press art");
    menu.class_key_release(6U);
    expect(menu.selected_class_index() == 6U && menu.visible_class_offset() == 2U &&
               near(menu.scroll_position(), 2.0),
           "key 7 selects the seventh class and reveals it");
    const auto draw = ClassSelectionPresentation{}.build(menu, {800, 600});
    expect(find_sprite(draw, "png/ui/icons/key3.png") != nullptr &&
               find_sprite(draw, "png/ui/icons/key1.png") == nullptr,
           "visible keys keep their absolute class numbers after scrolling");
    menu.class_key_release(0U);
    expect(menu.selected_class_index() == 0U && menu.visible_class_offset() == 0U,
           "key 1 always means the first class");
    menu.class_key_release(9U);
    expect(menu.selected_class_index() == 0U, "key 0 (tenth class) is inert for seven classes");
}

void server_class_data() {
    using namespace battlespades::world;
    const auto engineer = class_prefab_names(12U);
    expect(engineer.size() == 7U &&
               std::ranges::find(engineer, std::string_view{"prefab_superbridge"}) == engineer.end() &&
               std::ranges::find(engineer, std::string_view{"prefab_superpole"}) == engineer.end(),
           "Engineer offers the stock seven constructs from the server table");
    expect(class_name_key(12U) == "ENGINEER2" && class_name_key(2U) == "ENGINEER" &&
               class_description_key(12U) == "ENGINEER_DESCRIPTION",
           "class names/descriptions use the retail string ids");
    expect(tool_name_key(7U) == "SUB_MACHINE_GUN" && tool_description_key(7U) == "SMG_TOOL_DESCRIPTION",
           "tool popups use TOOL_NAMES / TOOL_DESCRIPTIONS");

    ClassSelectionRules classic;
    classic.disabled_tools = {22U, 37U, 38U};
    const auto rifles = class_row_options(5U, 1U, classic);
    expect(rifles.size() == 1U && rifles.front() == 6U,
           "server-disabled classic SMG/shotgun disappear from the Deuce row");
    expect(class_construct_options(5U, classic).empty(), "Deuce has no constructs and no flare tile");

    ClassSelectionRules open;
    auto constructs = class_construct_options(12U, open);
    expect(constructs.size() == 8U && constructs.front() == flare_block_construct,
           "the flare block is the first construct when the server enables it");
    const std::vector<std::string> picked{std::string{flare_block_construct}, "prefab_caltrop"};
    const std::vector<std::uint16_t> rows{0U, 7U, 16U, 68U};
    const auto with_flare = make_class_selection(12U, rows, picked, open);
    expect(std::ranges::find(with_flare.loadout, flare_block_tool) != with_flare.loadout.end() &&
               with_flare.prefabs == std::vector<std::string>{"prefab_caltrop"},
           "the flare tile becomes tool 22, not a prefab name");
    expect(with_flare.loadout.front() == 5U, "BLOCK_TOOL leads the loadout");

    ClassSelectionRules no_prefabs;
    no_prefabs.disabled_tools = {22U, 23U};
    expect(class_construct_options(12U, no_prefabs).empty() &&
               default_class_constructs(12U, no_prefabs).empty(),
           "a disabled PREFAB_TOOL hides the whole constructs table");

    ClassSelectionRules mafia;
    mafia.mafia = true;
    mafia.disabled_tools = {22U};
    const auto gangster = automatic_class_selection(6U, mafia);
    expect(gangster.prefabs.empty(), "mafia classes start with zero constructs");
    expect(std::ranges::find(gangster.loadout, flare_block_tool) == gangster.loadout.end(),
           "mafia never appends the flare block");
    ClassSelectionRules normal;
    const auto zombie = automatic_class_selection(4U, normal);
    expect(zombie.class_id == 4U && std::ranges::find(zombie.loadout, 24U) != zombie.loadout.end(),
           "a locked zombie team spawns with the zombie hand");

    ClassSelectionMenuModel menu;
    constexpr std::array<std::uint8_t, 1U> deuce{5U};
    menu.configure(deuce, 2U, 5U, classic, true, true);
    expect(!menu.select_enabled(), "a locked_class team disables SELECT");
    expect(!menu.click({650, 480}).has_value(), "a disabled SELECT does not submit");
    expect(menu.row_options(1U).size() == 1U, "the menu honours the disabled tool list");
    expect(menu.click({400, 540}) == battlespades::frontend::ClassSelectionAction::back,
           "the in-game BACK button (338,522 125x45) backs out");
}

void popups() {
    using Clock = ClassSelectionMenuModel::Clock;
    ClassSelectionMenuModel menu;
    constexpr std::array<std::uint8_t, 3U> classes{0U, 12U, 17U};
    menu.configure(classes, 2U, 0U);
    const auto start = Clock::time_point{} + std::chrono::seconds{100};
    menu.pointer_move(battlespades::ui::Point{190, 360}, start); // Primary row, first cell.
    expect(menu.hovered_item().has_value(), "loadout cells are hover targets");
    expect(!menu.popup_visible(start + std::chrono::milliseconds{400}),
           "popups wait for the 0.5 s hover delay");
    const auto shown = start + std::chrono::milliseconds{600};
    expect(menu.popup_visible(shown), "popups appear after 0.5 s");
    auto draw = ClassSelectionPresentation{}.build(menu, {800, 600}, false, {}, shown);
    expect(find_sprite(draw, "png/ui/in_game_menus/select_class/item_info_frame.png") != nullptr &&
               find_sprite(draw, "png/ui/in_game_menus/select_class/frame_arrow.png") != nullptr &&
               has_text(draw, battlespades::world::tool_name_key(menu.row_options(1U).front())),
           "weapon popup shows the frame, arrow and TOOL_NAMES title");
    menu.pointer_move(battlespades::ui::Point{140, 200}, shown);
    const auto later = shown + std::chrono::seconds{1};
    draw = ClassSelectionPresentation{}.build(menu, {800, 600}, false, {}, later);
    expect(find_sprite(draw, "png/ui/in_game_menus/select_class/class_info_frame_left.png") != nullptr &&
               has_text(draw, "SOLDIER_DESCRIPTION"),
           "class popup shows the left frame and CLASS_DESCRIPTIONS");
    menu.pointer_move(std::nullopt, later);
    expect(!menu.popup_visible(later + std::chrono::seconds{1}), "leaving every item hides popups");
}

battlespades::frontend::TeamRosterPlayer player(std::string name, std::uint8_t id, std::int32_t score) {
    battlespades::frontend::TeamRosterPlayer row;
    row.name = std::move(name);
    row.player_id = id;
    row.score = score;
    return row;
}

void kick_vote_menu() {
    using namespace battlespades::frontend;
    ChangeTeamServerState state;
    state.team1_players = {player("A", 1U, 5), player("B", 2U, 9)};
    state.team2_players = {player("C", 3U, 0)};
    state.spectator_players = {player("D", 4U, 0)};
    const auto columns = kick_vote_roster(state);
    expect(columns[0U][0U]->player.name == "B" && columns[0U][1U]->player.name == "A",
           "rosters are score sorted");
    expect(columns[1U][15U].has_value() && columns[1U][15U]->team == 0U &&
               columns[1U][15U]->player.name == "D",
           "a lone spectator takes the right column's last spare row");
    const auto hit = kick_vote_roster_hit(columns, {100, 200});
    expect(hit.has_value() && hit->row.player.player_id == 2U &&
               near(hit->bounds.y + hit->bounds.height * 0.5, 199.875),
           "row 1 is centred 16.4375 px below the list header");
    expect(!kick_vote_roster_hit(columns, {100, 180}).has_value(), "the header row is not a player");

    KickVoteMenuModel menu;
    menu.open(state);
    expect(menu.visible() && !menu.kick_enabled(), "KICK PLAYER starts disabled");
    menu.pointer_press(battlespades::ui::Point{340, 490});
    expect(!menu.pointer_release(battlespades::ui::Point{340, 490}).has_value(),
           "a disabled KICK PLAYER sends nothing");
    menu.pointer_press(battlespades::ui::Point{100, 200});
    static_cast<void>(menu.pointer_release(battlespades::ui::Point{100, 200}));
    expect(menu.kick_enabled() && menu.selection()->row.player.player_id == 2U,
           "clicking a row selects that player");
    menu.pointer_press(battlespades::ui::Point{265, 492});
    static_cast<void>(menu.pointer_release(battlespades::ui::Point{265, 492}));
    expect(menu.dropdown_open(), "the down-arrow opens the reason list");
    const auto hacking = KickVoteMenuModel::reason_row(1U);
    menu.pointer_press(battlespades::ui::Point{hacking.x + 20, hacking.y + 5});
    static_cast<void>(menu.pointer_release(battlespades::ui::Point{hacking.x + 20, hacking.y + 5}));
    expect(!menu.dropdown_open() && menu.reason() == KickVoteReason::hacking,
           "a reason row selects and closes the list");
    const auto draw = KickVoteMenuPresentation{}.build(menu);
    expect(find_sprite(draw, "png/ui/in_game_menus/change_team_content_frames.png") != nullptr &&
               has_text(draw, "SELECT_PLAYER_TO_KICK") && has_text(draw, "KICK_PLAYER") &&
               has_text(draw, "KICK_REASON_HACKING") &&
               find_sprite(draw, "png/ui/common_elements/buttons/highlight_scoreboard_blue.png") !=
                   nullptr,
           "the kick screen is the ChangeTeam-style roster with retail art");
    menu.pointer_press(battlespades::ui::Point{340, 490});
    const auto vote = menu.pointer_release(battlespades::ui::Point{340, 490});
    expect(vote.has_value() && vote->target_id == 2U && vote->reason == KickVoteReason::hacking,
           "KICK PLAYER returns the InitiateKick target and reason");

    menu.notify_localised_message("KICK_DENIED_REASON_VOTE_IN_PROGRESS");
    expect(!menu.tick(0.3) && menu.tick(0.3) && !menu.visible(),
           "a KICK_DENIED LocalisedMessage closes the menu 0.5 s later");
    menu.open(state);
    menu.notify_localised_message("SOMETHING_ELSE");
    expect(!menu.close_countdown().has_value(), "other messages do not close the menu");
}

void key_names() {
    using battlespades::settings::InputBinding;
    using battlespades::settings::retail_binding_name_id;
    using battlespades::settings::retail_key_name_id;
    expect(retail_key_name_id(54U) == "COMMA" && retail_key_name_id(55U) == "PERIOD",
           "comma/period use the retail names");
    expect(retail_key_name_id(224U) == "CTRL" && retail_key_name_id(225U) == "SHIFT",
           "KEY_TRANSLATIONS: left ctrl/shift");
    expect(retail_key_name_id(80U) == "LEFT" && retail_key_name_id(82U) == "UP" &&
               retail_key_name_id(41U) == "ESCAPE" && retail_key_name_id(40U) == "RETURN",
           "arrows, escape and return");
    expect(retail_key_name_id(9U) == "F" && retail_key_name_id(61U) == "F4" &&
               retail_key_name_id(19U) == "P" && retail_key_name_id(39U) == "0",
           "every letter, F-key and digit has a name (no SCANCODE N)");
    expect(retail_key_name_id(75U) == "PAGE_UP" && retail_key_name_id(57U) == "CAPS_LOCK",
           "PAGEUP/CAPSLOCK translations");
    for (std::uint32_t code{4U}; code <= 82U; ++code) {
        if (code >= 58U && code <= 69U) continue;
        expect(!retail_key_name_id(code).empty(), "no common keyboard key is unnamed");
    }
    expect(retail_binding_name_id(InputBinding::unbound()) == "NONE", "unbound shows strings.NONE");
    expect(retail_binding_name_id(InputBinding::mouse(3U)) == "RMB", "mouse buttons keep LMB/RMB");
    expect(retail_binding_name_id(InputBinding::keyboard(54U)) == "COMMA", "bindings resolve keys");

    using battlespades::frontend::settings_binding_text;
    expect(settings_binding_text(InputBinding::keyboard(54U), {}) == "COMMA" &&
               settings_binding_text(InputBinding::unbound(), {}) == "NONE",
           "without a catalogue the Controls value is the translate_key id");
    const battlespades::settings::RetailStringLookup english =
        [](std::string_view id) -> std::optional<std::string> {
        if (id == "LEFT") return std::string{"Left"};
        return std::nullopt;
    };
    expect(settings_binding_text(InputBinding::keyboard(80U), english) == "LITERAL|Left" &&
               settings_binding_text(InputBinding::keyboard(41U), english) == "LITERAL|ESCAPE" &&
               settings_binding_text(InputBinding::keyboard(228U), english) == "LITERAL|CTRL",
           "with a catalogue the value is final text: Left, ESCAPE (never humanised), RCTRL = CTRL");
}

} // namespace

int main() {
    try {
        scrollbar_maths();
        absolute_class_keys();
        server_class_data();
        popups();
        kick_vote_menu();
        key_names();
        std::cout << "retail menus: scrollbar, keys, class data, popups, kick vote, key names passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "retail menus failure: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
