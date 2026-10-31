#pragma once

#include "battlespades/ui/draw_list.hpp"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace battlespades::frontend {

struct ChatLink final {
    std::size_t begin{}, end{}; // UTF-8 byte offsets, end exclusive.
    std::string url;
};

/** Only Discord invite and channel destinations become interactive. */
[[nodiscard]] std::vector<ChatLink> discord_chat_links(std::string_view text);
/** A multiline clipboard becomes one chat line without joining words together. */
[[nodiscard]] std::string chat_clipboard_line(std::string_view text);

enum class ChatEditMotion { left, right, home, end };

/** Single-line UTF-8 editing with a byte/code-point bounded wire payload. */
class ChatTextEditor final {
public:
    [[nodiscard]] const std::string& text() const noexcept { return text_; }
    [[nodiscard]] bool empty() const noexcept { return text_.empty(); }
    [[nodiscard]] std::size_t caret() const noexcept { return caret_; }
    [[nodiscard]] std::pair<std::size_t, std::size_t> selection() const noexcept;
    [[nodiscard]] std::string selected_text() const;
    void clear() noexcept;
    [[nodiscard]] std::string take_text();
    void select_all() noexcept;
    void set_caret(std::size_t byte, bool extend = false) noexcept;
    void move(ChatEditMotion motion, bool extend, bool word = false) noexcept;
    [[nodiscard]] bool erase_selection();
    [[nodiscard]] bool erase(bool forward);
    [[nodiscard]] bool insert(std::string_view text, std::size_t maximum_bytes,
                              std::size_t maximum_code_points,
                              const std::function<bool(std::string_view)>& glyph_filter = {});

private:
    std::string text_;
    std::size_t caret_{}, anchor_{};
};

using ChatMeasureText = std::function<double(std::string_view, double, std::string_view)>;

struct ChatDocumentLine final {
    std::uint64_t id{};
    std::string text;
    bool continuation{};
};

/** Shared transcript selection/hit testing for HUD pixels and lobby design pixels. */
class ChatTextInteraction final {
public:
    void reset() noexcept;
    void begin_layout(std::vector<ChatDocumentLine> document, ChatMeasureText measure);
    void add_line(std::uint64_t id, ui::DrawRect bounds, std::string_view font,
                  double font_size, std::vector<ChatLink> links = {}, bool fit_to_width = false);
    void set_input(ui::DrawRect bounds, std::string_view font, double font_size,
                   std::string_view text = {}, bool fit_to_width = false);
    /** Paint before text; uses the same font measurements as pointer hit testing. */
    void decorate_line(ui::DrawList& list, std::uint64_t id, ui::DrawSpace space) const;
    void decorate_input(ui::DrawList& list, const ChatTextEditor& editor,
                        ui::DrawSpace space, bool active) const;
    [[nodiscard]] bool pointer_press(double x, double y, ChatTextEditor& editor);
    [[nodiscard]] bool pointer_move(double x, double y, ChatTextEditor& editor);
    [[nodiscard]] std::optional<std::string> pointer_release(double x, double y,
                                                            ChatTextEditor& editor);
    void cancel_drag() noexcept;
    void focus_input() noexcept;
    void select_all(ChatTextEditor& editor);
    [[nodiscard]] std::string selected_text(const ChatTextEditor& editor) const;
    [[nodiscard]] bool transcript_focused() const noexcept { return transcript_focus_; }
    [[nodiscard]] bool dragging() const noexcept { return dragging_; }

private:
    struct Position { std::uint64_t id{}; std::size_t byte{}; };
    struct Line {
        std::uint64_t id{};
        ui::DrawRect bounds;
        std::string font;
        double font_size{};
        std::vector<ChatLink> links;
        double advance_scale{1.0};
    };
    [[nodiscard]] const ChatDocumentLine* document_line(std::uint64_t id) const;
    [[nodiscard]] const Line* line_at(double x, double y, bool nearest) const;
    [[nodiscard]] double width(const Line& line, std::string_view text) const;
    [[nodiscard]] std::size_t byte_at(const Line& line, std::string_view text, double x) const;
    [[nodiscard]] std::pair<Position, Position> ordered_selection() const;
    [[nodiscard]] std::pair<std::size_t, std::size_t> selected_range(std::uint64_t id) const;
    [[nodiscard]] std::optional<std::string> link_at(double x, double y) const;
    std::vector<ChatDocumentLine> document_;
    std::vector<Line> lines_;
    ChatMeasureText measure_;
    Line input_;
    std::optional<Position> anchor_, focus_;
    std::optional<std::string> pressed_link_;
    double press_x_{}, press_y_{};
    bool transcript_focus_{}, dragging_{}, dragged_{};
};

} // namespace battlespades::frontend
