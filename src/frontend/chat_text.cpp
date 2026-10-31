#include "battlespades/frontend/chat_text.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>

namespace battlespades::frontend {
namespace {

std::size_t next_character(std::string_view text, std::size_t byte) noexcept {
    if (byte >= text.size()) return text.size();
    ++byte;
    while (byte < text.size() && (static_cast<unsigned char>(text[byte]) & 0xc0U) == 0x80U) ++byte;
    return byte;
}

std::size_t previous_character(std::string_view text, std::size_t byte) noexcept {
    if (byte == 0U) return 0U;
    --byte;
    while (byte > 0U && (static_cast<unsigned char>(text[byte]) & 0xc0U) == 0x80U) --byte;
    return byte;
}

bool valid_character(std::string_view value) noexcept {
    if (value.empty()) return false;
    const auto first = static_cast<unsigned char>(value.front());
    const auto length = first < 0x80U ? 1U : first >= 0xc2U && first <= 0xdfU ? 2U
                        : first >= 0xe0U && first <= 0xefU ? 3U
                        : first >= 0xf0U && first <= 0xf4U ? 4U : 0U;
    if (value.size() != length) return false;
    std::uint32_t code = first & (length == 1U ? 0x7fU : length == 2U ? 0x1fU : length == 3U ? 0x0fU : 0x07U);
    for (std::size_t i = 1; i < value.size(); ++i) {
        const auto byte = static_cast<unsigned char>(value[i]);
        if ((byte & 0xc0U) != 0x80U) return false;
        code = (code << 6U) | (byte & 0x3fU);
    }
    return (length != 2U || code >= 0x80U) && (length != 3U || code >= 0x800U) &&
           (length != 4U || code >= 0x10000U) && code <= 0x10ffffU &&
           !(code >= 0xd800U && code <= 0xdfffU);
}

bool contains(ui::DrawRect bounds, double x, double y) noexcept {
    return bounds.width > 0.0 && bounds.height > 0.0 && x >= bounds.x && y >= bounds.y &&
           x <= bounds.x + bounds.width && y <= bounds.y + bounds.height;
}

void solid(ui::DrawList& list, ui::DrawRect bounds, ui::DrawSpace space, ui::ColorRgba8 color) {
    if (bounds.width <= 0.0 || bounds.height <= 0.0) return;
    ui::SpriteDrawCommand command;
    command.asset_id = "png/high/white.png";
    command.destination = bounds;
    command.space = space;
    command.modulation.color = color;
    list.push(std::move(command));
}

bool word_space(std::string_view text, std::size_t byte) noexcept {
    return byte < text.size() && std::isspace(static_cast<unsigned char>(text[byte])) != 0;
}

} // namespace

std::vector<ChatLink> discord_chat_links(std::string_view text) {
    std::vector<ChatLink> result;
    for (std::size_t start = 0; start < text.size();) {
        while (start < text.size() && (std::isspace(static_cast<unsigned char>(text[start])) ||
               std::string_view{"(<[\"'"}.find(text[start]) != std::string_view::npos)) ++start;
        auto end = start;
        while (end < text.size() && !std::isspace(static_cast<unsigned char>(text[end])) &&
               std::string_view{"<>\"'"}.find(text[end]) == std::string_view::npos) ++end;
        auto trimmed = end;
        while (trimmed > start && std::string_view{".,!?;:)]}"}.find(text[trimmed-1U]) != std::string_view::npos) --trimmed;
        auto token = text.substr(start, trimmed-start);
        auto lower = std::string{token};
        std::ranges::transform(lower, lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        std::size_t prefix{};
        if (lower.starts_with("https://")) prefix = 8U;
        else if (lower.starts_with("http://")) prefix = 7U;
        const auto authority = std::string_view{lower}.substr(prefix);
        const auto slash = authority.find('/');
        const auto host = authority.substr(0, slash);
        const auto path = slash == std::string_view::npos ? std::string_view{} : authority.substr(slash);
        const bool invite_host = host == "discord.gg" || host == "www.discord.gg";
        const bool discord_host = host == "discord.com" || host == "www.discord.com" ||
                                  host == "discordapp.com" || host == "www.discordapp.com";
        const bool allowed = (invite_host && path.size() > 1U) ||
            (discord_host && ((path.starts_with("/invite/") && path.size() > 8U) ||
                              (path.starts_with("/channels/") && path.size() > 10U)));
        // Exact hosts only: credentials, ports, backslashes, encoded authorities,
        // embedded controls and deceptive suffixes must remain ordinary text.
        const bool clean = std::ranges::all_of(token, [](unsigned char c) {
            return c >= 0x21U && c < 0x7fU && c != '\\';
        });
        if (allowed && clean) result.push_back({start, trimmed, "https://" + std::string{token.substr(prefix)}});
        start = end > start ? end : start + 1U;
    }
    return result;
}

std::string chat_clipboard_line(std::string_view text) {
    std::string line;
    bool separated{};
    for (const char value : text) {
        if (value == '\r' || value == '\n' || value == '\t') {
            if (!separated) line += ' ';
            separated = true;
        } else {
            line += value;
            separated = false;
        }
    }
    return line;
}

std::pair<std::size_t, std::size_t> ChatTextEditor::selection() const noexcept {
    return std::minmax(caret_, anchor_);
}
std::string ChatTextEditor::selected_text() const {
    const auto [begin, end] = selection();
    return text_.substr(begin, end-begin);
}
void ChatTextEditor::clear() noexcept { text_.clear(); caret_ = anchor_ = 0U; }
std::string ChatTextEditor::take_text() { auto value = std::move(text_); clear(); return value; }
void ChatTextEditor::select_all() noexcept { anchor_ = 0U; caret_ = text_.size(); }
void ChatTextEditor::set_caret(std::size_t byte, bool extend) noexcept {
    byte = std::min(byte, text_.size());
    while (byte > 0U && byte < text_.size() && (static_cast<unsigned char>(text_[byte]) & 0xc0U) == 0x80U) --byte;
    caret_ = byte;
    if (!extend) anchor_ = caret_;
}
void ChatTextEditor::move(ChatEditMotion motion, bool extend, bool word) noexcept {
    auto byte = caret_;
    const auto [begin, end] = selection();
    if (!extend && begin != end && (motion == ChatEditMotion::left || motion == ChatEditMotion::right)) {
        byte = motion == ChatEditMotion::left ? begin : end;
    } else if (motion == ChatEditMotion::home) byte = 0U;
    else if (motion == ChatEditMotion::end) byte = text_.size();
    else if (motion == ChatEditMotion::left) {
        if (word) while (byte > 0U && word_space(text_, previous_character(text_, byte))) byte = previous_character(text_, byte);
        do { byte = previous_character(text_, byte); }
        while (word && byte > 0U && !word_space(text_, previous_character(text_, byte)));
    } else {
        do { byte = next_character(text_, byte); }
        while (word && byte < text_.size() && !word_space(text_, byte));
        if (word) while (byte < text_.size() && word_space(text_, byte)) byte = next_character(text_, byte);
    }
    set_caret(byte, extend);
}
bool ChatTextEditor::erase_selection() {
    const auto [begin, end] = selection();
    if (begin == end) return false;
    text_.erase(begin, end-begin); caret_ = anchor_ = begin; return true;
}
bool ChatTextEditor::erase(bool forward) {
    if (erase_selection()) return true;
    const auto begin = forward ? caret_ : previous_character(text_, caret_);
    const auto end = forward ? next_character(text_, caret_) : caret_;
    if (begin == end) return false;
    text_.erase(begin, end-begin); caret_ = anchor_ = begin; return true;
}
bool ChatTextEditor::insert(std::string_view value, std::size_t maximum_bytes,
                            std::size_t maximum_code_points,
                            const std::function<bool(std::string_view)>& glyph_filter) {
    const auto [begin, end] = selection();
    const auto retained = text_.substr(0U, begin) + text_.substr(end);
    auto characters = static_cast<std::size_t>(std::ranges::count_if(retained, [](unsigned char c) { return (c & 0xc0U) != 0x80U; }));
    std::string accepted;
    for (std::size_t byte{}; byte < value.size();) {
        const auto next = next_character(value, byte);
        const auto character = value.substr(byte, next-byte);
        byte = next;
        if (!valid_character(character)) continue;
        const auto first = static_cast<unsigned char>(character.front());
        if (first < 0x20U || first == 0x7fU || (glyph_filter && !glyph_filter(character))) continue;
        if (characters >= maximum_code_points || retained.size() + accepted.size() + character.size() > maximum_bytes) break;
        accepted += character; ++characters;
    }
    if (accepted.empty()) return false;
    text_.replace(begin, end-begin, accepted);
    caret_ = anchor_ = begin + accepted.size();
    return true;
}

void ChatTextInteraction::reset() noexcept {
    anchor_.reset(); focus_.reset(); cancel_drag(); transcript_focus_ = false;
}
void ChatTextInteraction::begin_layout(std::vector<ChatDocumentLine> document, ChatMeasureText measure) {
    if (anchor_ && focus_) {
        for (const auto position : {*anchor_, *focus_}) {
            const auto* previous = document_line(position.id);
            const auto next = std::ranges::find(document, position.id, &ChatDocumentLine::id);
            if (previous == nullptr || next == document.end() || previous->text != next->text) {
                reset(); break;
            }
        }
    }
    document_ = std::move(document); measure_ = std::move(measure); lines_.clear();
    std::ranges::sort(document_, {}, &ChatDocumentLine::id);
    if (anchor_ && (!focus_ || !document_line(anchor_->id) || !document_line(focus_->id))) reset();
}
const ChatDocumentLine* ChatTextInteraction::document_line(std::uint64_t id) const {
    const auto found = std::ranges::find(document_, id, &ChatDocumentLine::id);
    return found == document_.end() ? nullptr : &*found;
}
void ChatTextInteraction::add_line(std::uint64_t id, ui::DrawRect bounds, std::string_view font,
                                  double font_size, std::vector<ChatLink> links, bool fit_to_width) {
    lines_.push_back({id, bounds, std::string{font}, font_size, std::move(links)});
    if (fit_to_width) {
        if (const auto* doc = document_line(id)) {
            const auto full_width = width(lines_.back(), doc->text);
            if (full_width > bounds.width && full_width > 0.0)
                lines_.back().advance_scale = bounds.width / full_width;
        }
    }
}
void ChatTextInteraction::set_input(ui::DrawRect bounds, std::string_view font, double font_size,
                                   std::string_view text, bool fit_to_width) {
    input_ = {0U, bounds, std::string{font}, font_size, {}};
    const auto full_width = width(input_, text);
    if (fit_to_width && full_width > bounds.width && full_width > 0.0)
        input_.advance_scale = bounds.width / full_width;
}
double ChatTextInteraction::width(const Line& line, std::string_view text) const {
    const auto measured = measure_ ? measure_(text, line.font_size, line.font) :
        static_cast<double>(std::ranges::count_if(text, [](unsigned char c) { return (c & 0xc0U) != 0x80U; })) * line.font_size * 0.5;
    return std::isfinite(measured) ? std::max(0.0, measured) * line.advance_scale : 0.0;
}
std::size_t ChatTextInteraction::byte_at(const Line& line, std::string_view text, double x) const {
    std::vector<std::size_t> offsets{0U};
    for (std::size_t byte{}; byte < text.size();) { byte = next_character(text, byte); offsets.push_back(byte); }
    const auto target = std::max(0.0, x-line.bounds.x);
    auto low = std::size_t{}, high = offsets.size()-1U;
    while (low < high) {
        const auto mid = (low+high)/2U;
        const auto left = width(line, text.substr(0U, offsets[mid]));
        const auto right = width(line, text.substr(0U, offsets[mid+1U]));
        if (target < (left+right)*0.5) high = mid; else low = mid+1U;
    }
    return offsets[low];
}
const ChatTextInteraction::Line* ChatTextInteraction::line_at(double x, double y, bool nearest) const {
    const Line* best{};
    auto distance = std::numeric_limits<double>::max();
    for (const auto& line : lines_) {
        if (!document_line(line.id)) continue;
        if (contains(line.bounds, x, y)) return &line;
        const auto delta = std::abs(y-(line.bounds.y+line.bounds.height*0.5));
        if (nearest && delta < distance) { distance = delta; best = &line; }
    }
    return best;
}
std::pair<ChatTextInteraction::Position, ChatTextInteraction::Position> ChatTextInteraction::ordered_selection() const {
    const auto less = [](Position a, Position b) { return a.id < b.id || (a.id == b.id && a.byte < b.byte); };
    return less(*anchor_, *focus_) ? std::pair{*anchor_, *focus_} : std::pair{*focus_, *anchor_};
}
std::pair<std::size_t, std::size_t> ChatTextInteraction::selected_range(std::uint64_t id) const {
    const auto* doc = document_line(id);
    if (!anchor_ || !focus_ || !doc) return {};
    const auto [begin, end] = ordered_selection();
    if (id < begin.id || id > end.id) return {};
    return {id == begin.id ? begin.byte : 0U, id == end.id ? end.byte : doc->text.size()};
}
void ChatTextInteraction::decorate_line(ui::DrawList& list, std::uint64_t id, ui::DrawSpace space) const {
    const auto found = std::ranges::find(lines_, id, &Line::id);
    const auto* doc = document_line(id);
    if (found == lines_.end() || !doc) return;
    const auto& line = *found;
    const auto [begin, end] = selected_range(id);
    const auto left = width(line, std::string_view{doc->text}.substr(0U, begin));
    const auto right = width(line, std::string_view{doc->text}.substr(0U, end));
    solid(list, {line.bounds.x+left, line.bounds.y, std::min(right, line.bounds.width)-left, line.bounds.height}, space, {55U,105U,180U,190U});
    for (const auto& link : line.links) {
        const auto x = width(line, std::string_view{doc->text}.substr(0U, link.begin));
        const auto far = width(line, std::string_view{doc->text}.substr(0U, link.end));
        solid(list, {line.bounds.x+x, line.bounds.y+line.bounds.height-1.0,
                     std::min(far, line.bounds.width)-x, 1.0}, space, {135U,170U,255U,255U});
    }
}
void ChatTextInteraction::decorate_input(ui::DrawList& list, const ChatTextEditor& editor,
                                        ui::DrawSpace space, bool active) const {
    if (!active || transcript_focus_) return;
    const auto [begin, end] = editor.selection();
    const auto left = width(input_, std::string_view{editor.text()}.substr(0U, begin));
    const auto right = width(input_, std::string_view{editor.text()}.substr(0U, end));
    solid(list, {input_.bounds.x+left, input_.bounds.y, std::min(right, input_.bounds.width)-left,
                 input_.bounds.height}, space, {55U,105U,180U,190U});
    const auto caret = width(input_, std::string_view{editor.text()}.substr(0U, editor.caret()));
    if (caret <= input_.bounds.width)
        solid(list, {input_.bounds.x+caret, input_.bounds.y, 1.0, input_.bounds.height}, space, {255U,255U,255U,255U});
}
std::optional<std::string> ChatTextInteraction::link_at(double x, double y) const {
    const auto* line = line_at(x, y, false);
    if (!line) return {};
    const auto* doc = document_line(line->id);
    const auto byte = byte_at(*line, doc->text, x);
    for (const auto& link : line->links) {
        const auto left = line->bounds.x + width(*line, std::string_view{doc->text}.substr(0U, link.begin));
        const auto right = line->bounds.x + width(*line, std::string_view{doc->text}.substr(0U, link.end));
        if (byte >= link.begin && byte <= link.end && x >= left && x < right) return link.url;
    }
    return {};
}
bool ChatTextInteraction::pointer_press(double x, double y, ChatTextEditor& editor) {
    cancel_drag();
    if (contains(input_.bounds, x, y)) {
        focus_input(); editor.set_caret(byte_at(input_, editor.text(), x));
    } else if (const auto* line = line_at(x, y, false)) {
        transcript_focus_ = true;
        const auto* doc = document_line(line->id);
        anchor_ = focus_ = Position{line->id, byte_at(*line, doc->text, x)};
        editor.set_caret(editor.caret());
        pressed_link_ = link_at(x, y);
    } else return false;
    press_x_ = x; press_y_ = y; dragging_ = true; return true;
}
bool ChatTextInteraction::pointer_move(double x, double y, ChatTextEditor& editor) {
    if (!dragging_) return false;
    dragged_ = dragged_ || std::hypot(x-press_x_, y-press_y_) >= 3.0;
    if (transcript_focus_) {
        if (const auto* line = line_at(x, y, true))
            focus_ = Position{line->id, byte_at(*line, document_line(line->id)->text, x)};
    } else editor.set_caret(byte_at(input_, editor.text(), x), true);
    return true;
}
std::optional<std::string> ChatTextInteraction::pointer_release(double x, double y, ChatTextEditor& editor) {
    if (!dragging_) return {};
    static_cast<void>(pointer_move(x, y, editor));
    const auto link = !dragged_ && pressed_link_ && pressed_link_ == link_at(x, y) ? pressed_link_ : std::nullopt;
    cancel_drag(); return link;
}
void ChatTextInteraction::cancel_drag() noexcept { dragging_ = dragged_ = false; pressed_link_.reset(); }
void ChatTextInteraction::focus_input() noexcept { transcript_focus_ = false; anchor_.reset(); focus_.reset(); }
void ChatTextInteraction::select_all(ChatTextEditor& editor) {
    if (transcript_focus_ && !document_.empty()) {
        anchor_ = Position{document_.front().id, 0U};
        focus_ = Position{document_.back().id, document_.back().text.size()};
    } else { focus_input(); editor.select_all(); }
}
std::string ChatTextInteraction::selected_text(const ChatTextEditor& editor) const {
    if (!transcript_focus_ || !anchor_ || !focus_) return editor.selected_text();
    std::string selected;
    const auto [first, last] = ordered_selection();
    bool included{};
    for (const auto& line : document_) {
        if (line.id < first.id || line.id > last.id) continue;
        const auto [begin, end] = selected_range(line.id);
        if (included && !line.continuation) selected += '\n';
        selected += line.text.substr(begin, end-begin);
        included = true;
    }
    return selected;
}

} // namespace battlespades::frontend
