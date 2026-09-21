#pragma once

#include "battlespades/ui/input.hpp"
#include "battlespades/ui/widget.hpp"
#include "battlespades/world/class_selection.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <map>
#include <span>
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

/** Renderer-neutral state for the stock SelectClass join gate. */
class ClassSelectionMenuModel final {
public:
    void configure(std::span<const std::uint8_t> available_classes,
                   std::uint8_t team,
                   std::uint8_t current_class);
    /** Restore the authoritative player's choices when reopening SelectClass. */
    void restore_loadout(std::span<const std::uint8_t> loadout,
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
    [[nodiscard]] std::span<const std::string> selected_prefabs() const noexcept;
    [[nodiscard]] world::ClassSelection selection() const;
    [[nodiscard]] std::optional<ui::Point> hovered() const noexcept;
    /** Consume at most one cue after each input event. */
    [[nodiscard]] std::optional<ClassSelectionAudioCue> take_audio_cue() noexcept;

    void pointer_move(std::optional<ui::Point> point) noexcept;
    [[nodiscard]] std::optional<ClassSelectionAction> click(ui::Point point);
    void cycle_class(int direction) noexcept;
    /** Move retail's five-card viewport without changing the selected class. */
    void scroll_classes(int direction) noexcept;
    void cycle_group(std::size_t group, int direction) noexcept;
    void select_visible_class(std::size_t visible_index) noexcept;

private:
    void select_class(std::size_t index) noexcept;
    void reveal_selected_class() noexcept;
    void reset_prefabs();

    std::vector<std::uint8_t> classes_;
    std::size_t class_index_{};
    std::size_t visible_class_offset_{};
    std::uint8_t team_{2U};
    std::array<std::size_t, 4U> option_indices_{};
    std::vector<std::string> prefabs_;
    struct SavedLoadout {
        std::array<std::size_t, 4U> options{};
        std::vector<std::string> prefabs;
    };
    std::map<std::uint8_t, SavedLoadout> class_loadouts_;
    std::optional<ui::Point> hovered_;
    std::optional<ClassSelectionAudioCue> pending_audio_cue_;
};

} // namespace battlespades::frontend
