#include "battlespades/frontend/resolution_confirmation.hpp"

#include <algorithm>

namespace battlespades::frontend {
namespace {

[[nodiscard]] constexpr ui::Rect retail_button(std::int32_t x,
                                               std::int32_t bottom_y,
                                               std::int32_t width,
                                               std::int32_t height) noexcept {
    constexpr auto scale = ResolutionConfirmationModel::subpixels_per_pixel;
    return {
        x * scale,
        (ResolutionConfirmationModel::reference_height_pixels - bottom_y - height) * scale,
        width * scale,
        height * scale,
    };
}

[[nodiscard]] bool contains_strict(ui::Rect bounds, ui::Point point) noexcept {
    return point.x > bounds.x && point.y > bounds.y &&
           point.x < bounds.x + bounds.width && point.y < bounds.y + bounds.height;
}

constexpr auto keep_bounds = retail_button(161, 108, 236, 61);
constexpr auto revert_bounds = retail_button(402, 108, 236, 61);

} // namespace

ResolutionConfirmationModel::ResolutionConfirmationModel() noexcept = default;

void ResolutionConfirmationModel::restart() noexcept {
    remaining_ = timeout;
    hovered_ = Hit::none;
    armed_ = Hit::none;
    expired_emitted_ = false;
}

std::optional<ResolutionConfirmationAction>
ResolutionConfirmationModel::tick(std::chrono::nanoseconds elapsed) noexcept {
    if (expired_emitted_) {
        return std::nullopt;
    }
    if (elapsed > std::chrono::nanoseconds::zero()) {
        remaining_ = elapsed >= remaining_ ? std::chrono::nanoseconds::zero()
                                           : remaining_ - elapsed;
    }
    if (remaining_ == std::chrono::nanoseconds::zero()) {
        expired_emitted_ = true;
        armed_ = Hit::none;
        return ResolutionConfirmationAction::revert;
    }
    return std::nullopt;
}

void ResolutionConfirmationModel::pointer_move(std::optional<ui::Point> point) noexcept {
    hovered_ = hit_test(point);
}

void ResolutionConfirmationModel::pointer_press(std::optional<ui::Point> point) noexcept {
    pointer_move(point);
    armed_ = hovered_;
}

std::optional<ResolutionConfirmationAction>
ResolutionConfirmationModel::pointer_release(std::optional<ui::Point> point) noexcept {
    pointer_move(point);
    const auto activated = armed_ != Hit::none && armed_ == hovered_ ? armed_ : Hit::none;
    armed_ = Hit::none;
    if (activated == Hit::keep) {
        return ResolutionConfirmationAction::keep;
    }
    if (activated == Hit::revert) {
        return ResolutionConfirmationAction::revert;
    }
    return std::nullopt;
}

void ResolutionConfirmationModel::cancel_pointer_capture() noexcept {
    armed_ = Hit::none;
    hovered_ = Hit::none;
}

std::uint32_t ResolutionConfirmationModel::seconds_remaining() const noexcept {
    // Retail formats int(remaining): 15 only on the first frame, then 14..0.
    const auto whole = std::chrono::duration_cast<std::chrono::seconds>(remaining_).count();
    return static_cast<std::uint32_t>(std::max<std::int64_t>(whole, 0));
}

bool ResolutionConfirmationModel::keep_hovered() const noexcept {
    return hovered_ == Hit::keep;
}

bool ResolutionConfirmationModel::revert_hovered() const noexcept {
    return hovered_ == Hit::revert;
}

bool ResolutionConfirmationModel::keep_pressed() const noexcept {
    return armed_ == Hit::keep && hovered_ == Hit::keep;
}

bool ResolutionConfirmationModel::revert_pressed() const noexcept {
    return armed_ == Hit::revert && hovered_ == Hit::revert;
}

ResolutionConfirmationModel::Hit
ResolutionConfirmationModel::hit_test(std::optional<ui::Point> point) noexcept {
    if (!point.has_value()) {
        return Hit::none;
    }
    if (contains_strict(keep_bounds, *point)) {
        return Hit::keep;
    }
    if (contains_strict(revert_bounds, *point)) {
        return Hit::revert;
    }
    return Hit::none;
}

} // namespace battlespades::frontend
