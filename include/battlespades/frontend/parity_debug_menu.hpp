#pragma once

#include "battlespades/ui/geometry.hpp"
#include "battlespades/ui/input.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace battlespades::frontend {

/** Classification used by the recovered retail frontend inventory. */
enum class ParityCatalogKind : std::uint8_t {
    screen,
    component,
    widget,
    service,
};

/** One immutable retail class/route entry shown by the parity browser. */
struct ParityCatalogEntry final {
    std::string id;
    std::string retail_class;
    ParityCatalogKind kind{ParityCatalogKind::screen};
    /** Stable recovered route; empty only for non-screen vocabulary entries. */
    std::string route;
    std::string parent_screen;
    std::string source;
    std::vector<std::string> widgets;
    std::vector<std::string> assets;
    std::vector<std::string> substates;
    /** True only when the native client has a deterministic screen fixture. */
    bool native_fixture{};
};

/** Checked-in inventory generated from the decompiled retail client. */
[[nodiscard]] std::span<const ParityCatalogEntry> retail_frontend_catalog() noexcept;

enum class ParityDebugFilter : std::uint8_t {
    all,
    screens,
    components,
    widgets,
    services,
};

enum class ParityDebugAction : std::uint8_t {
    back,
    open_native_fixture,
    toggle_hitboxes,
};

struct ParityDebugActivation final {
    ParityDebugAction action{ParityDebugAction::back};
    std::string entry_id;
};

/** Renderer-neutral state for the F12 retail UI parity browser. */
class ParityDebugMenuModel final {
public:
    static constexpr std::size_t visible_rows{13U};

    ParityDebugMenuModel();

    [[nodiscard]] ParityDebugFilter filter() const noexcept;
    [[nodiscard]] std::string_view filter_label() const noexcept;
    [[nodiscard]] std::span<const std::size_t> filtered_indices() const noexcept;
    [[nodiscard]] std::size_t first_visible_row() const noexcept;
    [[nodiscard]] std::optional<std::size_t> selected_filtered_row() const noexcept;
    [[nodiscard]] const ParityCatalogEntry* selected_entry() const noexcept;
    [[nodiscard]] bool show_hitboxes() const noexcept;
    [[nodiscard]] std::size_t detail_first_row() const noexcept;
    [[nodiscard]] std::size_t detail_line_count() const noexcept;

    void cycle_filter(std::int32_t direction);
    [[nodiscard]] bool select_filtered_row(std::size_t row) noexcept;
    [[nodiscard]] bool select_visible_row(std::size_t row) noexcept;
    void scroll_rows(std::int32_t delta) noexcept;
    void scroll_details(std::int32_t delta) noexcept;

    void pointer_move(std::optional<ui::Point> point) noexcept;
    void pointer_press(std::optional<ui::Point> point) noexcept;
    [[nodiscard]] std::optional<ParityDebugActivation>
    pointer_release(std::optional<ui::Point> point) noexcept;
    [[nodiscard]] std::optional<ParityDebugActivation> handle(ui::InputEvent event);

    [[nodiscard]] std::optional<std::size_t> hovered_visible_row() const noexcept;
    [[nodiscard]] bool filter_hovered() const noexcept;
    [[nodiscard]] bool open_hovered() const noexcept;
    [[nodiscard]] bool hitbox_hovered() const noexcept;

private:
    void rebuild_filter();
    void reveal_selection() noexcept;
    [[nodiscard]] std::optional<std::size_t> hit_visible_row(ui::Point point) const noexcept;
    [[nodiscard]] std::optional<ParityDebugActivation> activate_at(ui::Point point);

    ParityDebugFilter filter_{ParityDebugFilter::all};
    std::vector<std::size_t> filtered_indices_;
    std::optional<std::size_t> selected_row_{};
    std::size_t first_visible_row_{};
    std::size_t detail_first_row_{};
    std::optional<std::size_t> hovered_visible_row_{};
    bool show_hitboxes_{};
    bool filter_hovered_{};
    bool open_hovered_{};
    bool hitbox_hovered_{};
    bool pointer_down_{};
};

[[nodiscard]] std::string_view parity_kind_label(ParityCatalogKind kind) noexcept;

} // namespace battlespades::frontend
