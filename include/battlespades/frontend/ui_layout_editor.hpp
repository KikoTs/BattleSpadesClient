#pragma once

#include "battlespades/ui/draw_list.hpp"
#include "battlespades/ui/geometry.hpp"

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace battlespades::frontend {

/** One stable, editable draw-command rectangle exposed by the offline editor. */
struct UiLayoutElement final {
    std::string id;
    ui::DrawRect bounds{};
    ui::DrawSpace space{ui::DrawSpace::design_pixels};
    std::size_t command_index{};
};

/**
 * External, versioned UI rectangle overrides.
 *
 * Presentations remain the authoritative recovered retail defaults.  This
 * service applies only explicitly saved absolute rectangles after a screen is
 * built, so deleting ui-layout.json immediately returns to source parity.
 * Files are size bounded, validated and fail closed; malformed user edits
 * never replace the last valid in-memory document.
 */
class UiLayoutStore final {
public:
    explicit UiLayoutStore(std::filesystem::path path = {});

    [[nodiscard]] const std::filesystem::path& path() const noexcept;
    [[nodiscard]] bool load();
    [[nodiscard]] bool reload_if_changed();
    [[nodiscard]] bool save();

    void apply(std::string_view screen_id, ui::DrawList& list) const;
    [[nodiscard]] std::vector<UiLayoutElement>
    elements(std::string_view screen_id, const ui::DrawList& list) const;

    void set(std::string id, ui::DrawRect bounds);
    [[nodiscard]] bool erase(std::string_view id);
    void reset_screen(std::string_view screen_id);

    [[nodiscard]] bool dirty() const noexcept;
    [[nodiscard]] std::size_t override_count() const noexcept;
    [[nodiscard]] std::string_view last_error() const noexcept;

private:
    std::filesystem::path path_;
    std::map<std::string, ui::DrawRect, std::less<>> overrides_;
    std::optional<std::filesystem::file_time_type> loaded_write_time_;
    bool dirty_{};
    std::string last_error_;
};

/**
 * Mouse/keyboard controller for the offline layout surface.
 *
 * The editor accepts design-pixel input only and never exists in a live
 * network match.  Drag moves an item; dragging its lower-right handle resizes
 * it. Ctrl+S writes the external JSON, Ctrl+R reloads it, arrows nudge and
 * Shift+arrows resize. F11 toggling is owned by the native frontend.
 */
class UiLayoutEditor final {
public:
    explicit UiLayoutEditor(UiLayoutStore& store);

    void set_active(bool active) noexcept;
    [[nodiscard]] bool active() const noexcept;
    [[nodiscard]] bool dragging() const noexcept;

    void observe(std::string screen_id, const ui::DrawList& list);
    [[nodiscard]] bool pointer_press(ui::Point point);
    [[nodiscard]] bool pointer_move(ui::Point point);
    [[nodiscard]] bool pointer_release(ui::Point point);
    [[nodiscard]] bool nudge(std::int32_t x, std::int32_t y, bool resize);
    [[nodiscard]] bool save();
    [[nodiscard]] bool reload();
    [[nodiscard]] bool reset_selected();
    void decorate(ui::DrawList& list) const;

    [[nodiscard]] std::string_view selected_id() const noexcept;
    [[nodiscard]] std::string_view status() const noexcept;

private:
    [[nodiscard]] UiLayoutElement* selected_element() noexcept;
    [[nodiscard]] const UiLayoutElement* selected_element() const noexcept;

    UiLayoutStore& store_;
    bool active_{};
    bool drag_resize_{};
    std::string screen_id_;
    std::vector<UiLayoutElement> elements_;
    std::string selected_id_;
    std::optional<ui::Point> drag_origin_;
    ui::DrawRect drag_bounds_{};
    std::string status_;
};

} // namespace battlespades::frontend
