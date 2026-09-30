#pragma once

#include "battlespades/ui/input.hpp"
#include "battlespades/ui/widget.hpp"
#include "battlespades/world/class_selection.hpp"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace battlespades::frontend {

enum class ClassSelectionAction : std::uint8_t { submit, back };

/** Retail HUD-zone cues emitted by SelectClass interactions. */
enum class ClassSelectionAudioCue : std::uint8_t { scroll, confirm, back };

/** Recovered HorizontalListSelection geometry, shared by drawing and input. */
struct ClassSelectionCardLayout final {
    int x{}, y{}, size{}, interval{}, name_x{}, name_width{};
    double image_scale{}, frame_scale{};
};

/**
 * HorizontalScrollBar(70, 340, 660, 20, N, 5) in top-left 800x600 pixels
 * (aoslib/gui.py ScrollBar.fixup_props + HorizontalScrollBar.draw).
 */
struct ClassScrollbarGeometry final {
    /** Black 660x22 backing quad. */
    ui::Rect frame{70, 249, 660, 22};
    /** (73,63,7) channel: bar_full_length 612 x bar_thickness 20. */
    ui::Rect channel{94, 250, 612, 20};
    ui::Rect dec_button{70, 249, 22, 22};
    ui::Rect inc_button{708, 249, 22, 22};
    /** bar_coord for the current float scroll position (thumb left edge). */
    double thumb_x{93.0};
    /** bar_current_length = floor(612 * min(1, 5/N)). */
    double thumb_length{612.0};
    double coord_at_min{93.0};
    double coord_at_max{93.0};
    bool dec_enabled{};
    bool inc_enabled{};
};

/** floor(612 * min(100, 100*visible/N) / 100): retail thumb length. */
[[nodiscard]] double retail_class_thumb_length(std::size_t classes,
                                               std::size_t visible = 5U) noexcept;
/**
 * ScrollBar.set_scroll(value, set_as_int=False): the integer viewport a drag
 * lands on. 0 at/below 0, max at/above max, else int(1 + v*(max-1)/max).
 */
[[nodiscard]] std::size_t retail_drag_scroll_index(double value, std::size_t max_scroll) noexcept;

enum class ClassScrollButton : std::uint8_t { dec, inc };

/** The item under the pointer; SelectClass shows its popup after 0.5 s. */
struct ClassSelectionHover final {
    enum class Kind : std::uint8_t { class_card, loadout_item, construct };
    Kind kind{Kind::class_card};
    /** Loadout row (0..3) for loadout_item. */
    std::size_t group{};
    /** Absolute class index, row option index or construct index. */
    std::size_t index{};
    bool operator==(const ClassSelectionHover&) const = default;
};

/** Renderer-neutral state for the stock SelectClass screen. */
class ClassSelectionMenuModel final {
public:
    using Clock = std::chrono::steady_clock;
    /** selectClass.update: popups draw once the hover is older than 0.5 s. */
    static constexpr double popup_delay_seconds{0.5};
    /** TableSelection(…, 3 per row, 12 per page). */
    static constexpr std::size_t constructs_per_page{12U};

    /**
     * `rules` carries InitialInfo.disabled_tools and the mafia/UGC mode.
     * `locked_class` is the team's TeamLockClass state (SELECT disabled).
     * `in_game` selects the in-game frame + BACK button instead of the navbar.
     */
    void configure(std::span<const std::uint8_t> available_classes,
                   std::uint8_t team,
                   std::uint8_t current_class,
                   world::ClassSelectionRules rules = {},
                   bool locked_class = false,
                   bool in_game = false);
    /**
     * Restore a playing replica's matching class when reopening SelectClass.
     * Spectator/default replicas have no active playing loadout and must not
     * replace the saved choices loaded by configure().
     */
    void restore_playing_loadout(std::uint8_t player_team,
                                 std::uint8_t player_class,
                                 std::span<const std::uint8_t> loadout,
                                 std::span<const std::string> prefabs);

    [[nodiscard]] std::span<const std::uint8_t> classes() const noexcept;
    [[nodiscard]] std::uint8_t team() const noexcept;
    [[nodiscard]] std::uint8_t selected_class() const noexcept;
    [[nodiscard]] std::size_t selected_class_index() const noexcept;
    [[nodiscard]] std::size_t classes_per_page() const noexcept;
    [[nodiscard]] ClassSelectionCardLayout card_layout() const noexcept;
    [[nodiscard]] ui::Rect class_card_bounds(std::size_t visible_index) const noexcept;
    [[nodiscard]] std::size_t visible_class_offset() const noexcept;
    [[nodiscard]] std::array<std::size_t, 4U> option_indices() const noexcept;
    /** One loadout row after the server's disabled tools were removed. */
    [[nodiscard]] std::span<const std::uint16_t> row_options(std::size_t group) const noexcept;
    /** Constructs table: flare tile (if enabled), class constructs, map prefabs. */
    [[nodiscard]] std::span<const std::string> construct_options() const noexcept;
    [[nodiscard]] std::size_t construct_page() const noexcept { return construct_page_; }
    [[nodiscard]] std::size_t construct_page_count() const noexcept;
    [[nodiscard]] std::span<const std::string> selected_prefabs() const noexcept;
    [[nodiscard]] world::ClassSelection selection() const;
    /**
     * What SelectClass.create_loadout_list wrote to the config during this
     * visit: every class the player switched away from (on_class_selected
     * saves before it changes the class) and, once SELECT is pressed
     * (`confirmed`), the shown class.
     */
    [[nodiscard]] std::map<std::uint8_t, world::SavedClassLoadout>
    session_loadouts(bool confirmed) const;
    [[nodiscard]] const world::ClassSelectionRules& rules() const noexcept { return rules_; }
    [[nodiscard]] bool in_game() const noexcept { return in_game_; }
    /** selectClass.draw: SELECT is disabled while the team is locked_class. */
    [[nodiscard]] bool select_enabled() const noexcept { return !locked_class_; }
    [[nodiscard]] std::optional<ui::Point> hovered() const noexcept;
    [[nodiscard]] std::optional<ClassSelectionHover> hovered_item() const noexcept;
    [[nodiscard]] bool popup_visible(Clock::time_point now = Clock::now()) const noexcept;
    /** Consume at most one cue after each input event. */
    [[nodiscard]] std::optional<ClassSelectionAudioCue> take_audio_cue() noexcept;

    [[nodiscard]] bool has_scrollbar() const noexcept;
    [[nodiscard]] ClassScrollbarGeometry scrollbar_geometry() const noexcept;
    [[nodiscard]] double scroll_position() const noexcept { return scroll_float_; }
    [[nodiscard]] bool scrollbar_dragging() const noexcept { return dragging_; }
    [[nodiscard]] std::optional<ClassScrollButton> pressed_scroll_button() const noexcept {
        return pressed_button_;
    }
    [[nodiscard]] bool select_pressed() const noexcept { return select_pressed_; }
    [[nodiscard]] bool back_pressed() const noexcept { return back_pressed_; }
    /** Absolute class index whose number key is held (key_press art). */
    [[nodiscard]] std::optional<std::size_t> held_class_key() const noexcept { return held_key_; }

    /** In-game BACK TextButton(338, 78, 125, 45) in top-left pixels. */
    static constexpr ui::Rect in_game_back_bounds{338, 522, 125, 45};
    static constexpr ui::Rect select_bounds{599, 461, 124, 40};

    void pointer_move(std::optional<ui::Point> point, Clock::time_point now = Clock::now());
    void pointer_press(std::optional<ui::Point> point);
    [[nodiscard]] std::optional<ClassSelectionAction> pointer_release(std::optional<ui::Point> point);
    /** Press + release at one point. */
    [[nodiscard]] std::optional<ClassSelectionAction> click(ui::Point point);
    /** Abandon any press/drag (focus loss). */
    void cancel_pointer() noexcept;

    /**
     * Retail KeyDisplay: key n (0 = 10th) belongs to absolute class n-1, even
     * when that card is scrolled out of view. The press shows key_press art;
     * the release selects and scrolls just far enough to reveal the card.
     */
    void class_key_press(std::size_t absolute_index) noexcept;
    void class_key_release(std::size_t absolute_index) noexcept;

    void cycle_class(int direction) noexcept;
    /** Move retail's five-card viewport without changing the selected class. */
    void scroll_classes(int direction) noexcept;
    void cycle_group(std::size_t group, int direction) noexcept;
    void select_visible_class(std::size_t visible_index) noexcept;

private:
    void select_class(std::size_t index) noexcept;
    void reveal_selected_class() noexcept;
    void rebuild_options();
    void reset_loadout();
    void set_scroll_int(int value) noexcept;
    void drag_scroll_to(double x) noexcept;
    [[nodiscard]] std::optional<ClassSelectionHover> hit_item(ui::Point point) const noexcept;
    [[nodiscard]] std::optional<ClassScrollButton> hit_scroll_button(ui::Point point) const noexcept;
    [[nodiscard]] std::optional<ClassSelectionAction> activate(ui::Point point);

    std::vector<std::uint8_t> classes_;
    std::size_t class_index_{};
    std::size_t visible_class_offset_{};
    double scroll_float_{};
    std::uint8_t team_{2U};
    world::ClassSelectionRules rules_;
    bool locked_class_{};
    bool in_game_{};
    std::array<std::vector<std::uint16_t>, 4U> row_options_;
    std::array<std::size_t, 4U> option_indices_{};
    std::vector<std::string> construct_options_;
    std::size_t construct_page_{};
    std::vector<std::string> prefabs_;
    struct SavedLoadout {
        std::array<std::size_t, 4U> options{};
        std::vector<std::string> prefabs;
    };
    std::map<std::uint8_t, SavedLoadout> class_loadouts_;
    std::optional<ui::Point> hovered_;
    std::optional<ClassSelectionHover> hovered_item_;
    Clock::time_point hover_started_{};
    bool dragging_{};
    std::optional<ClassScrollButton> pressed_button_;
    bool select_pressed_{};
    bool back_pressed_{};
    std::optional<ui::Point> press_point_;
    std::optional<std::size_t> held_key_;
    std::optional<ClassSelectionAudioCue> pending_audio_cue_;
};

} // namespace battlespades::frontend
