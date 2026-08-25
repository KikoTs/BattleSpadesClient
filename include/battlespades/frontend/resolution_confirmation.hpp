#pragma once

#include "battlespades/ui/geometry.hpp"

#include <chrono>
#include <cstdint>
#include <optional>

namespace battlespades::frontend {

enum class ResolutionConfirmationAction : std::uint8_t {
    keep,
    revert,
};

/**
 * Deterministic model for retail's 15-second resolution safety screen.
 *
 * The model owns no window or configuration state. The frontend controller
 * applies Keep/Revert to its transaction after consuming the emitted action.
 * Keyboard input is intentionally absent: the recovered client ignored even
 * Escape here so an accidental key could not strand an unusable display mode.
 */
class ResolutionConfirmationModel final {
public:
    static constexpr std::int32_t reference_width_pixels{800};
    static constexpr std::int32_t reference_height_pixels{600};
    static constexpr std::int32_t subpixels_per_pixel{8};
    static constexpr std::chrono::seconds timeout{15};

    ResolutionConfirmationModel() noexcept;

    void restart() noexcept;
    [[nodiscard]] std::optional<ResolutionConfirmationAction>
    tick(std::chrono::nanoseconds elapsed) noexcept;

    void pointer_move(std::optional<ui::Point> point) noexcept;
    void pointer_press(std::optional<ui::Point> point) noexcept;
    [[nodiscard]] std::optional<ResolutionConfirmationAction>
    pointer_release(std::optional<ui::Point> point) noexcept;
    void cancel_pointer_capture() noexcept;

    [[nodiscard]] std::uint32_t seconds_remaining() const noexcept;
    [[nodiscard]] bool keep_hovered() const noexcept;
    [[nodiscard]] bool revert_hovered() const noexcept;
    [[nodiscard]] bool keep_pressed() const noexcept;
    [[nodiscard]] bool revert_pressed() const noexcept;

private:
    enum class Hit : std::uint8_t { none, keep, revert };
    [[nodiscard]] static Hit hit_test(std::optional<ui::Point> point) noexcept;

    std::chrono::nanoseconds remaining_{timeout};
    Hit hovered_{Hit::none};
    Hit armed_{Hit::none};
    bool expired_emitted_{};
};

} // namespace battlespades::frontend
