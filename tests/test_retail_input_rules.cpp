#include "battlespades/frontend/game_hud.hpp"
#include "battlespades/frontend/fallback_music.hpp"
#include "battlespades/frontend/retail_announcer.hpp"
#include "battlespades/frontend/retail_input_rules.hpp"
#include "battlespades/settings/client_settings.hpp"
#include "battlespades/settings/retail_key_names.hpp"

#include <array>
#include <iostream>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

using battlespades::frontend::ControlKeyNames;
using battlespades::frontend::MatchStartStingerGate;
using battlespades::frontend::resolve_control_placeholders;
using battlespades::settings::ControlAction;
using battlespades::settings::InputBinding;
using battlespades::settings::RetailStringLookup;

void expect(bool value, const char* message) {
    if (!value) {
        throw std::runtime_error{message};
    }
}

// SDL scancodes used below.
constexpr std::uint32_t sc_e{8U};
constexpr std::uint32_t sc_f{9U};
constexpr std::uint32_t sc_g{10U};
constexpr std::uint32_t sc_k{14U};
constexpr std::uint32_t sc_p{19U};
constexpr std::uint32_t sc_r{21U};
constexpr std::uint32_t sc_escape{41U};
constexpr std::uint32_t sc_comma{54U};
constexpr std::uint32_t sc_period{55U};
constexpr std::uint32_t sc_f11{68U};
constexpr std::uint32_t sc_right{79U};
constexpr std::uint32_t sc_left{80U};
constexpr std::uint32_t sc_down{81U};
constexpr std::uint32_t sc_up{82U};
constexpr std::uint32_t sc_keypad_1{89U};
constexpr std::uint32_t sc_lctrl{224U};
constexpr std::uint32_t sc_lshift{225U};
constexpr std::uint32_t sc_rctrl{228U};
constexpr std::uint32_t sc_rshift{229U};

/** A tiny english.py subset: ids with a string resolve, others fall back. */
RetailStringLookup english_lookup() {
    static const std::map<std::string, std::string, std::less<>> strings{
        {"LEFT", "Left"},
        {"RIGHT", "Right"},
        {"UP", "UP"},
        {"DOWN", "DOWN"},
        {"COMMA", "COMMA"},
        {"PERIOD", "PERIOD"},
        {"CTRL", "CTRL"},
        {"SHIFT", "SHIFT"},
        {"SPACE", "SPACE"},
        {"TAB", "TAB"},
        {"RMB", "RMB"},
        {"TOOL_HELP_PANEL_CLOSE", "{key_tool_help} Close"},
        {"UGC_GAME_SETTINGS_HINT", "Press {key_ugc_settings} to open additional map settings."},
    };
    return [](std::string_view id) -> std::optional<std::string> {
        if (const auto it = strings.find(id); it != strings.end()) return it->second;
        return std::nullopt;
    };
}

RetailStringLookup german_lookup() {
    return [](std::string_view id) -> std::optional<std::string> {
        if (id == "LEFT") return std::string{"Links"};
        if (id == "TOOL_HELP_PANEL_CLOSE") return std::string{"{key_tool_help} Schliessen"};
        return std::nullopt;
    };
}

void key_names() {
    using battlespades::settings::retail_binding_name;
    using battlespades::settings::retail_key_name;
    using battlespades::settings::retail_key_string_id;
    const auto lookup = english_lookup();
    expect(retail_key_name(sc_comma, lookup) == "COMMA", "',' must read COMMA, not ','");
    expect(retail_key_name(sc_period, lookup) == "PERIOD", "'.' must read PERIOD");
    expect(retail_key_name(sc_escape, lookup) == "ESCAPE",
           "Escape has no english.py string, so translate_key keeps ESCAPE");
    expect(retail_key_name(sc_left, lookup) == "Left" && retail_key_name(sc_right, lookup) == "Right",
           "the arrows use the english LEFT/RIGHT strings");
    expect(retail_key_name(sc_up, lookup) == "UP" && retail_key_name(sc_down, lookup) == "DOWN",
           "UP/DOWN keep their upper-case strings");
    expect(retail_key_name(sc_lctrl, lookup) == "CTRL" && retail_key_name(sc_rctrl, lookup) == "CTRL",
           "KEY_TRANSLATIONS turns both Ctrl keys into CTRL");
    expect(retail_key_name(sc_lshift, lookup) == "SHIFT" &&
               retail_key_name(sc_rshift, lookup) == "SHIFT",
           "KEY_TRANSLATIONS turns both Shift keys into SHIFT");
    expect(retail_key_name(sc_f11, lookup) == "F11", "function keys keep their symbol name");
    expect(retail_key_name(sc_f, lookup) == "F" && retail_key_name(sc_p, lookup) == "P",
           "every letter has a name (no SCANCODE N)");
    expect(retail_key_name(sc_keypad_1, lookup) == "1", "keypad digits drop the NUM_ prefix");
    expect(retail_key_name(53U, lookup) == "QUOTELEFT", "grave uses the QUOTELEFT id");
    expect(retail_key_name(sc_left, german_lookup()) == "Links",
           "key names follow the active language catalogue");
    expect(retail_key_name(sc_left, {}) == "LEFT", "no catalogue falls back to the id");
    expect(retail_key_string_id(0U).empty(), "scancode 0 has no retail symbol");
    expect(retail_binding_name(InputBinding::unbound(), lookup).empty(),
           "an unbound (None) binding has no key text");
    expect(retail_binding_name(InputBinding::mouse(3U), lookup) == "RMB",
           "mouse button 3 is RMB");
}

void rebinding_is_honoured() {
    using battlespades::frontend::retail_aim_mouse_button;
    using battlespades::settings::retail_key_matches;
    auto settings = battlespades::settings::retail_default_settings();
    auto& controls = settings.controls;
    // Retail defaults.
    expect(retail_key_matches(controls, ControlAction::reload, sc_r), "R reloads by default");
    expect(retail_key_matches(controls, ControlAction::weapon_custom, sc_e),
           "E picks colour by default");
    expect(retail_key_matches(controls, ControlAction::menu, sc_escape),
           "Escape opens the menu by default");
    expect(retail_aim_mouse_button(controls, 3U), "RMB always aims");
    expect(retail_key_matches(controls, ControlAction::crouch, sc_rctrl),
           "right Ctrl is the same key as the Ctrl binding in retail");

    // Rebind all four controls and verify the old keys stop firing.
    expect(controls.set_binding(ControlAction::reload, InputBinding::keyboard(sc_f)),
           "reload rebinding accepted");
    expect(controls.set_binding(ControlAction::weapon_custom, InputBinding::keyboard(sc_g)),
           "pick colour rebinding accepted");
    expect(controls.set_binding(ControlAction::menu, InputBinding::keyboard(sc_p)),
           "menu rebinding accepted");
    expect(controls.set_binding(ControlAction::aim, InputBinding::keyboard(sc_k)),
           "aim rebinding accepted");
    expect(retail_key_matches(controls, ControlAction::reload, sc_f) &&
               !retail_key_matches(controls, ControlAction::reload, sc_r),
           "only the new Reload key reloads");
    expect(retail_key_matches(controls, ControlAction::weapon_custom, sc_g) &&
               !retail_key_matches(controls, ControlAction::weapon_custom, sc_e),
           "only the new Pick Colour key picks");
    expect(retail_key_matches(controls, ControlAction::menu, sc_p) &&
               !retail_key_matches(controls, ControlAction::menu, sc_escape),
           "only the new Menu key opens the menu");
    expect(retail_key_matches(controls, ControlAction::aim, sc_k),
           "a key bound to Aim aims");
    expect(retail_aim_mouse_button(controls, 3U) && !retail_aim_mouse_button(controls, 2U),
           "RMB keeps aiming alongside the extra key; other buttons do not");
    expect(controls.set_binding(ControlAction::aim, InputBinding::mouse(2U)),
           "aim mouse rebinding accepted");
    expect(retail_aim_mouse_button(controls, 2U) && retail_aim_mouse_button(controls, 3U),
           "a mouse button bound to Aim adds to RMB");
    const auto names =
        battlespades::frontend::retail_control_key_names(controls, english_lookup());
    expect(names.menu == "P", "the {key_menu} hint follows the rebinding");
    expect(names.weapon_custom == "G", "the {key_weapon_custom} hint follows the rebinding");
}

void placeholder_resolver() {
    using battlespades::frontend::retail_control_key_names;
    const auto settings = battlespades::settings::retail_default_settings();
    const auto names = retail_control_key_names(settings.controls, english_lookup());
    // Every one of retail's 20 translate_controls_in_message controls.
    const std::array<std::pair<std::string_view, std::string_view>, 20U> expected{{
        {"{key_forward}", "[W]"},
        {"{key_backward}", "[S]"},
        {"{key_left}", "[A]"},
        {"{key_right}", "[D]"},
        {"{key_jump}", "[SPACE]"},
        {"{key_crouch}", "[CTRL]"},
        {"{key_change_class}", "[COMMA]"},
        {"{key_view_scores}", "[TAB]"},
        {"{key_palette_up}", "[UP]"},
        {"{key_palette_down}", "[DOWN]"},
        {"{key_palette_left}", "[Left]"},
        {"{key_palette_right}", "[Right]"},
        {"{key_weapon_custom}", "[E]"},
        {"{key_cancel_prefab_placement}", "[Q]"},
        {"{key_carve_prefab}", "[C]"},
        {"{key_tool_help}", "[H]"},
        {"{key_hover}", "[Z]"},
        {"{key_sprint}", "[SHIFT]"},
        {"{key_ugc_settings}", "[X]"},
        {"{key_menu}", "[ESCAPE]"},
    }};
    for (const auto& [token, rendered] : expected) {
        if (resolve_control_placeholders(token, names) != rendered) {
            std::cerr << "placeholder " << token << " rendered "
                      << resolve_control_placeholders(token, names) << '\n';
            expect(false, "every retail {key_*} placeholder must resolve");
        }
    }
    expect(resolve_control_placeholders("Press {key_change_class} to change class", names) ==
               "Press [COMMA] to change class",
           "the death hint must read [COMMA]");
    expect(resolve_control_placeholders("Target destroyed. {0} left.", names) ==
               "Target destroyed. {0} left.",
           "format fields are not control placeholders");
    expect(resolve_control_placeholders("{key_voice_record} {", names) == "{key_voice_record} {",
           "unknown placeholders and stray braces stay literal, as in retail");
    const ControlKeyNames defaults;
    expect(resolve_control_placeholders("{key_crouch}", defaults) == "[CTRL]",
           "the default crouch name is retail's CTRL");
}

void help_strings() {
    using battlespades::frontend::retail_help_string;
    const auto lookup = english_lookup();
    expect(retail_help_string("UGC_GAME_SETTINGS_HINT", lookup) ==
               "Press {key_ugc_settings} to open additional map settings.",
           "non-tutorial HelpMessage ids resolve through the catalogue");
    expect(retail_help_string("TOOL_HELP_PANEL_CLOSE", german_lookup()) ==
               "{key_tool_help} Schliessen",
           "the help footer is localised");
    expect(retail_help_string("TUTORIAL_JUMP_1", {}) == "Use {key_jump} to jump.",
           "the recovered English table remains the fallback");
    expect(retail_help_string("NOT_A_STRING", lookup) == "NOT_A_STRING",
           "an unknown id renders as the id, like get_by_id");
}

void ugc_tool_tips() {
    using battlespades::frontend::ugc_tool_help_ids;
    const auto paint = ugc_tool_help_ids(43U, false);
    expect(paint.size() == 1U && paint.front() == "UGC_HELP_PAINTBRUSH", "paintbrush tip");
    expect(ugc_tool_help_ids(42U, false).front() == "UGC_HELP_CONSTRUCTTOOL_STEP1" &&
               ugc_tool_help_ids(42U, true).front() == "UGC_HELP_CONSTRUCTTOOL_STEP2",
           "the construct tool steps with prefab control");
    expect(ugc_tool_help_ids(5U, false).front() == "UGC_HELP_BLOCKTOOL", "block tool tip");
    expect(ugc_tool_help_ids(48U, false).front() == "UGC_HELP_BLOCKCANNON", "block cannon tip");
    expect(ugc_tool_help_ids(41U, false).front() == "UGC_HELP_GAMEDATATOOL", "game data tip");
    expect(ugc_tool_help_ids(0U, false).empty(), "tools without tips have none");
}

void stinger_once_per_match() {
    MatchStartStingerGate gate;
    expect(!gate.update(false, false), "loading plays nothing");
    expect(gate.update(true, false), "the first SelectTeam plays the stinger");
    expect(!gate.update(true, false), "staying in the menu does not replay it");
    expect(!gate.update(false, false), "SelectClass plays nothing");
    expect(gate.update(true, false),
           "backing out of the initial SelectClass re-enters SelectTeam and replays it");
    expect(!gate.update(false, true), "spawning plays nothing");
    expect(!gate.update(true, false), "the in-game '.' ChangeTeam never plays it");
    expect(!gate.update(false, true), "returning to the world plays nothing");
    expect(!gate.update(true, false), "a second '.' still plays nothing");
    gate.new_match();
    expect(gate.update(true, false), "the next match's SelectTeam plays it again");
}

void fallback_music_ownership() {
    using battlespades::frontend::FallbackMusic;
    using enum battlespades::frontend::FallbackMusicAction;
    FallbackMusic music;
    expect(music.update(false, true, false) == none, "silent servers stay silent by default");
    expect(music.update(true, false, false) == none, "enabling in menus preserves menu music");
    expect(music.update(true, true, false) == start, "start on entering a silent match");
    const auto first = music.choose_track(2U);
    expect(music.update(true, true, false) == none, "pause, respawn and ticks must not restart music");
    expect(music.update(false, true, false) == stop, "disable or cancel stops only fallback music");
    expect(music.update(true, true, false) == start, "re-enabling starts the fallback again");
    expect(music.choose_track(2U) != first, "do not immediately repeat the same gameplay track");
    expect(music.update(true, true, true) == none, "server music takes ownership without a stop");
    expect(music.update(false, true, true) == none, "disabling must not stop the server's track");
    expect(music.update(true, true, true) == none, "server StopMusic must remain respected");
    music.relinquish();
    expect(music.update(true, false, false) == none, "map loading does not play fallback music");
    expect(music.update(true, true, false) == start, "new silent map can play music again");
    expect(music.update(true, false, false) == stop, "results and disconnect release fallback music");
    expect(music.update(true, false, false) == none, "do not repeatedly stop subsequent menu music");
}

void music_and_keys() {
    using battlespades::frontend::developer_chord;
    using battlespades::frontend::retail_server_music_allowed;
    expect(!retail_server_music_allowed(true),
           "PlayMusic/StopMusic are ignored while secondary_menu_bed_001 plays");
    expect(retail_server_music_allowed(false), "server music plays otherwise");
    expect(!developer_chord(0U) && !developer_chord(0x0001U) && !developer_chord(0x0040U),
           "plain or single-modifier F-keys stay retail keys");
    expect(developer_chord(0x0041U) && developer_chord(0x0082U),
           "Ctrl+Shift opens developer tools");
    const auto path = battlespades::frontend::retail_screenshot_path("shots", "MayanJungle", 2U);
    expect(path.filename() == "MayanJungle2.png", "take_screenshot names <map><index>.png");
    expect(battlespades::frontend::retail_screenshot_path("shots", "a/b:c", 0U).filename() ==
               "a_b_c0.png",
           "path separators in a map name cannot escape the directory");
}

} // namespace

int main() {
    try {
        key_names();
        rebinding_is_honoured();
        placeholder_resolver();
        help_strings();
        ugc_tool_tips();
        stinger_once_per_match();
        music_and_keys();
        fallback_music_ownership();
    } catch (const std::exception& error) {
        std::cerr << "retail input rules test failed: " << error.what() << '\n';
        return 1;
    }
    std::cout << "retail input rules tests passed\n";
    return 0;
}
