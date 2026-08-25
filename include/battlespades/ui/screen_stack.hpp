#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace battlespades::ui {

/** Stable screen identity. Zero is reserved as an invalid/unassigned value. */
struct ScreenId final {
    std::uint32_t value{};

    [[nodiscard]] constexpr bool is_valid() const noexcept {
        return value != 0U;
    }

    [[nodiscard]] friend constexpr bool operator==(const ScreenId&, const ScreenId&) = default;
};

enum class ScreenOperation {
    push,
    pop,
    replace,
    reset,
};

/** A data-only route mutation; pop ignores target and all others require it. */
struct ScreenCommand final {
    ScreenOperation operation{ScreenOperation::pop};
    ScreenId target{};

    [[nodiscard]] static constexpr ScreenCommand push(ScreenId id) noexcept {
        return ScreenCommand{ScreenOperation::push, id};
    }

    [[nodiscard]] static constexpr ScreenCommand pop() noexcept {
        return ScreenCommand{ScreenOperation::pop, {}};
    }

    [[nodiscard]] static constexpr ScreenCommand replace(ScreenId id) noexcept {
        return ScreenCommand{ScreenOperation::replace, id};
    }

    [[nodiscard]] static constexpr ScreenCommand reset(ScreenId id) noexcept {
        return ScreenCommand{ScreenOperation::reset, id};
    }
};

/**
 * Bounded, deterministic navigation stack with no screen-object ownership.
 *
 * The fixed-tick UI controller applies commands after dispatching its input
 * frame. Batch application is transactional, preventing half-applied routes
 * when a malformed command or depth overflow is encountered.
 */
class ScreenStack final {
public:
    explicit ScreenStack(std::size_t maximum_depth = 16U);

    [[nodiscard]] std::size_t size() const noexcept;
    [[nodiscard]] bool empty() const noexcept;
    [[nodiscard]] std::optional<ScreenId> top() const noexcept;
    [[nodiscard]] std::span<const ScreenId> screens() const noexcept;

    [[nodiscard]] bool apply(ScreenCommand command);
    [[nodiscard]] bool apply(std::span<const ScreenCommand> commands);

private:
    [[nodiscard]] bool apply_to(std::vector<ScreenId>& screens, ScreenCommand command) const;

    std::size_t maximum_depth_;
    std::vector<ScreenId> screens_;
};

} // namespace battlespades::ui
