#pragma once

#include "battlespades/ui/geometry.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace battlespades::frontend {

/**
 * The loading screen's server password box.
 *
 * Retail LoadingMenu builds it as
 * `EditBoxControl('', 70, 180, 350, 20, center=False, empty_text='Type the
 * server password and press enter to continue.', draw_background=False)` with
 * `is_password = True` and navigation_font; PasswordNeeded empties and shows
 * it, Enter sends PasswordProvided with the text and hides it again. (The
 * shipped retail build has that packet branch commented out.)
 */
class ServerPasswordPromptModel final {
public:
    /** The server refuses anything longer without comparing it. */
    static constexpr std::size_t maximum_bytes{64U};
    /** EditBoxControl(70, 180, 350, 20), bottom-left origin, as top-left pixels. */
    static constexpr ui::Rect box_bounds{70, 400, 350, 20};
    /** Retail's empty_text; shown while nothing is typed. */
    static constexpr std::string_view hint_key{"SERVER_PASSWORD_PROMPT"};
    /** Shown above the box after the server refused an answer. */
    static constexpr std::string_view wrong_key{"SERVER_PASSWORD_WRONG"};

    /** PasswordNeeded: empty the box and show it. */
    void open(bool rejected);
    /** The answer was sent, the join went on, or the connection ended. */
    void close() noexcept;

    [[nodiscard]] bool visible() const noexcept { return visible_; }
    /** The server refused the previous answer. */
    [[nodiscard]] bool rejected() const noexcept { return rejected_; }
    [[nodiscard]] bool empty() const noexcept { return text_.empty(); }
    /** One '*' per typed character; the text itself is never drawn. */
    [[nodiscard]] std::string masked() const;

    /** Committed keyboard text or a paste; refused whole if it cannot fit. */
    [[nodiscard]] bool append_text(std::string_view text);
    /** Backspace: removes one whole UTF-8 character. */
    [[nodiscard]] bool erase_character() noexcept;
    /**
     * Enter. Hands the typed password over and hides the box; an empty box
     * stays open and returns nothing.
     */
    [[nodiscard]] std::optional<std::string> submit();

private:
    void wipe() noexcept;

    std::string text_;
    bool visible_{};
    bool rejected_{};
};

/** A server address with the password a link or command line carried. */
struct EndpointWithPassword final {
    std::string endpoint;
    std::string password;
};

/**
 * Split `host:port?password=secret` (also after `aos://`). The password is
 * percent-decoded. Text without the suffix is returned unchanged with no
 * password; a suffix that is not a password is left in place for the endpoint
 * parser to refuse.
 */
[[nodiscard]] EndpointWithPassword split_endpoint_password(std::string_view text);

} // namespace battlespades::frontend
