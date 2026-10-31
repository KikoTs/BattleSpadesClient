#include "battlespades/frontend/chat_text.hpp"
#include "battlespades/frontend/create_match_menu.hpp"
#include "battlespades/frontend/match_overlays.hpp"

#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace battlespades::frontend;

namespace {
void expect(bool condition, const char* message) {
    if (!condition) throw std::runtime_error{message};
}

double monospace(std::string_view text, double, std::string_view) {
    std::size_t count{};
    for (const unsigned char byte : text) if ((byte & 0xc0U) != 0x80U) ++count;
    return static_cast<double>(count) * 6.0;
}

void editor_selects_replaces_and_copies_unicode() {
    ChatTextEditor editor;
    expect(editor.insert("a\xD0\x96\xE6\x97\xA5z", 200U, 200U), "UTF-8 insert failed");
    editor.move(ChatEditMotion::left, false);
    editor.move(ChatEditMotion::left, true);
    expect(editor.selected_text() == "\xE6\x97\xA5", "Shift+Left split a UTF-8 character");
    expect(editor.insert("ok", 200U, 200U) && editor.text() == "a\xD0\x96okz",
           "typing must replace the selection at its caret");
    editor.set_caret(2U);
    expect(editor.caret() == 1U, "pointer caret must snap to a UTF-8 boundary");
    expect(editor.erase(true) && editor.text() == "aokz", "Delete must remove the next code point");
    editor.select_all();
    expect(editor.selected_text() == "aokz", "select all did not select the complete input");
    expect(editor.erase_selection() && editor.empty(), "cut selection did not clear input");
}

void editor_preserves_wire_limits_when_replacing() {
    expect(chat_clipboard_line("one\r\ntwo\tthree") == "one two three",
           "multiline paste must preserve word boundaries without sending extra messages");
    ChatTextEditor editor;
    expect(editor.insert(std::string(200U, 'a'), 200U, 200U), "fixture insert failed");
    expect(!editor.insert("b", 200U, 200U), "wire byte limit was exceeded");
    editor.select_all();
    expect(editor.insert("\xD0\x96", 200U, 200U) && editor.text() == "\xD0\x96",
           "replacement should reclaim the selected bytes");
    editor.select_all();
    expect(!editor.insert("\xC0\xAF\xED\xA0\x80", 200U, 200U) &&
           editor.text() == "\xD0\x96", "invalid UTF-8 must not destroy the selected text");
    editor.clear();
    expect(editor.insert("a#b\n\r\t", 200U, 200U,
                         [](std::string_view c) { return c != "#"; }) && editor.text() == "ab",
           "control bytes and unsupported glyphs entered the wire message");
}

void word_navigation_and_selection_collapse_are_predictable() {
    ChatTextEditor editor;
    static_cast<void>(editor.insert("one two three", 200U, 200U));
    editor.move(ChatEditMotion::left, true, true);
    expect(editor.selected_text() == "three", "Ctrl+Shift+Left must select a word");
    editor.move(ChatEditMotion::left, false);
    expect(editor.caret() == 8U && editor.selected_text().empty(), "Left must collapse to selection start");
    editor.move(ChatEditMotion::home, true);
    expect(editor.selected_text() == "one two ", "Shift+Home must retain its anchor");
    editor.move(ChatEditMotion::end, false);
    expect(editor.caret() == editor.text().size(), "End must restore the end caret");
}

void only_explicit_discord_destinations_are_links() {
    const auto links = discord_chat_links("Join (discord.gg/AbCd), https://discord.com/invite/a-B. https://discord.com/channels/1/2!");
    expect(links.size() == 3U && links[0].url == "https://discord.gg/AbCd" &&
           links[1].url == "https://discord.com/invite/a-B" &&
           links[2].url == "https://discord.com/channels/1/2", "Discord links or punctuation were parsed incorrectly");
    for (const auto value : {"https://example.com", "https://discord.gg.evil.test/code",
                            "https://discord.gg@evil.test/code", "https://evil.discord.gg/code",
                            "https://discord.gg:443/code", "https://discord.gg\\@evil.test/code",
                            "javascript:discord.gg/code", "file:///discord.gg/code",
                            "https://discord.com/invite/", "discord.gg/"})
        expect(discord_chat_links(value).empty(), "a non-Discord or malformed destination became clickable");
    expect(discord_chat_links("HTTP://DISCORDAPP.COM/invite/Code").front().url ==
           "https://DISCORDAPP.COM/invite/Code", "legacy Discord invitations must use HTTPS");
}

void selection_survives_new_messages_and_copy_keeps_line_order() {
    ChatTextInteraction interaction;
    ChatTextEditor editor;
    interaction.begin_layout({{1U,"hello",false}, {2U,"discord.gg/demo",false}}, monospace);
    interaction.add_line(1U, {10,10,300,15}, "font", 12);
    interaction.add_line(2U, {10,30,300,15}, "font", 12, discord_chat_links("discord.gg/demo"));
    expect(interaction.pointer_press(16,15,editor), "transcript press was not consumed");
    static_cast<void>(interaction.pointer_move(40,35,editor));
    expect(!interaction.pointer_release(40,35,editor), "dragging a Discord link opened it");
    expect(interaction.selected_text(editor) == "ello\ndisco", "multiline selection copied in the wrong order");
    interaction.begin_layout({{1U,"hello",false}, {2U,"discord.gg/demo",false}, {3U,"new",false}}, monospace);
    expect(interaction.selected_text(editor) == "ello\ndisco", "an incoming message destroyed selection");
    interaction.select_all(editor);
    expect(interaction.selected_text(editor) == "hello\ndiscord.gg/demo\nnew", "transcript Ctrl+A missed retained lines");
    interaction.begin_layout({{1U,"another room",false}}, monospace);
    expect(interaction.selected_text(editor).empty(), "reusing an ID in another room retained stale selection");
}

void links_require_press_and_release_on_the_same_link() {
    ChatTextInteraction interaction;
    ChatTextEditor editor;
    interaction.begin_layout({{1U,"discord.gg/demo example.com",false}}, monospace);
    interaction.add_line(1U, {10,10,300,15}, "font", 12,
                         discord_chat_links("discord.gg/demo example.com"));
    expect(interaction.pointer_press(22,15,editor), "link press was not consumed");
    expect(interaction.pointer_release(22,15,editor) == "https://discord.gg/demo", "Discord click did not activate");
    expect(!interaction.pointer_release(22,15,editor), "an unmatched release activated a link");
    static_cast<void>(interaction.pointer_press(112,15,editor));
    expect(!interaction.pointer_release(112,15,editor), "an ordinary URL became clickable");
    static_cast<void>(interaction.pointer_press(22,15,editor));
    interaction.cancel_drag();
    expect(!interaction.pointer_release(22,15,editor), "focus loss retained a link press");
}

void input_pointer_selection_and_transcript_copy_stay_separate() {
    ChatTextInteraction interaction;
    ChatTextEditor editor;
    static_cast<void>(editor.insert("abcdef",200U,200U));
    interaction.begin_layout({}, monospace);
    interaction.set_input({10,50,200,15}, "font", 12);
    expect(interaction.pointer_press(16,55,editor), "input press missed the input");
    static_cast<void>(interaction.pointer_move(34,55,editor));
    static_cast<void>(interaction.pointer_release(34,55,editor));
    expect(interaction.selected_text(editor) == "bcd", "input drag did not select by measured glyph widths");
    interaction.select_all(editor);
    expect(interaction.selected_text(editor) == "abcdef", "input Ctrl+A selected transcript instead");
}

void wrapped_messages_copy_without_inserting_newlines_into_links() {
    GameChatModel chat;
    const auto url = "https://discord.gg/" + std::string(110U, 'a');
    const auto message = std::string{"prefix "} + url;
    chat.add(message);
    chat.begin(ChatChannel::global);
    ChatTextInteraction interaction;
    static_cast<void>(GameChatPresentation{}.build(chat, {1280,720}, monospace, &interaction));
    expect(interaction.pointer_press(20,645,chat.input_editor()), "wrapped transcript hit missed");
    static_cast<void>(interaction.pointer_release(20,645,chat.input_editor()));
    interaction.select_all(chat.input_editor());
    expect(interaction.selected_text(chat.input_editor()) == message, "visual wrapping inserted bytes into copied message");
    std::size_t fragments{};
    for (const auto& entry : chat.entries()) for (const auto& link : entry.links) {
        ++fragments;
        expect(link.url == url, "wrapped link lost its full URL");
    }
    expect(fragments >= 2U, "fixture did not exercise a link across wrapped feed lines");
}

void lobby_input_and_history_use_the_same_editing_contract() {
    CreateMatchMenuModel lobby;
    lobby.focus_chat();
    expect(lobby.append_chat_text("hello world"), "lobby text input failed");
    lobby.chat_editor().select_all();
    expect(lobby.append_chat_text("discord.gg/demo"), "lobby paste did not replace selection");
    expect(lobby.chat_editor().text() == "discord.gg/demo", "lobby kept selected text after paste");
    lobby.set_chat_lines({{"A","first"},{"B","second"}});
    const auto before = lobby.presentation().chat_lines;
    lobby.set_chat_lines({{"A","first"},{"B","second"},{"A","third"}});
    const auto after = lobby.presentation().chat_lines;
    expect(before[0].id == after[0].id && before[1].id == after[1].id &&
           after[2].id > after[1].id, "lobby updates must preserve retained message identities");
}

void presentation_marks_only_discord_links_and_uses_fitted_hit_boxes() {
    GameChatModel chat;
    chat.add("https://example.com discord.gg/demo");
    chat.begin(ChatChannel::global);
    ChatTextInteraction interaction;
    const auto list = GameChatPresentation{}.build(chat, {1280,720}, monospace, &interaction);
    std::size_t underlines{};
    for (const auto& command : list.commands()) {
        const auto* sprite = std::get_if<battlespades::ui::SpriteDrawCommand>(&command);
        if (sprite && sprite->modulation.color == battlespades::ui::ColorRgba8{135U,170U,255U,255U})
            ++underlines;
    }
    expect(underlines == 1U, "presentation must underline Discord links alone");
    interaction.begin_layout({{1U,"discord.gg/demo",false}}, monospace);
    interaction.add_line(1U, {10,10,42,15}, "font", 12, discord_chat_links("discord.gg/demo"), true);
    ChatTextEditor editor;
    static_cast<void>(interaction.pointer_press(20,15,editor));
    expect(interaction.pointer_release(20,15,editor) == "https://discord.gg/demo",
           "shrunken lobby text must retain correctly fitted link hit boxes");
}
} // namespace

int main() {
    const std::vector<std::pair<const char*,std::function<void()>>> tests{
        {"Unicode selection and editing",editor_selects_replaces_and_copies_unicode},
        {"wire limits and malformed text",editor_preserves_wire_limits_when_replacing},
        {"word and caret navigation",word_navigation_and_selection_collapse_are_predictable},
        {"Discord destination allowlist",only_explicit_discord_destinations_are_links},
        {"stable multiline copy",selection_survives_new_messages_and_copy_keeps_line_order},
        {"explicit click activation",links_require_press_and_release_on_the_same_link},
        {"input pointer selection",input_pointer_selection_and_transcript_copy_stay_separate},
        {"wrapped Discord invitations",wrapped_messages_copy_without_inserting_newlines_into_links},
        {"lobby editing and history",lobby_input_and_history_use_the_same_editing_contract},
        {"presentation and fitted links",presentation_marks_only_discord_links_and_uses_fitted_hit_boxes},
    };
    unsigned failures{};
    for (const auto& [name,run] : tests) try { run(); std::cout << "[PASS] " << name << '\n'; }
    catch (const std::exception& error) { ++failures; std::cerr << "[FAIL] " << name << ": " << error.what() << '\n'; }
    return failures ? 1 : 0;
}
