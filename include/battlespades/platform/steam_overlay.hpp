#pragma once

#include <cstdint>

namespace battlespades::platform {

/** What the game must do this frame because Steam's overlay opened or closed. */
enum class SteamOverlayInputAction : std::uint8_t {
    /** Nothing changed since the last frame. */
    none,
    /**
     * The overlay just opened: release the mouse, show the cursor and drop
     * every held key, so the overlay receives the pointer and nothing the
     * player types into Steam chat moves or fires in the game.
     */
    suspend,
    /** The overlay closed: gameplay may capture the mouse again. */
    resume,
    /**
     * The overlay opened and closed between two frames. Capture is unchanged,
     * but keys pressed on the way in (Shift, Tab) must still be forgotten.
     */
    flush,
};

/**
 * Turns Steam's overlay state into frame-sized input decisions.
 *
 * GameOverlayActivated_t arrives on the Steam pump thread; the runtime keeps
 * the latest state and a count of activations, and the presentation thread
 * feeds both here once per frame. The count matters because an overlay
 * opened and closed inside one frame leaves the state unchanged, yet the keys
 * that opened it were seen by the game and would stay held.
 *
 * Pure logic with no Steam dependency, so it is tested on every machine.
 */
class SteamOverlayInputGate final {
public:
    /**
     * Observe the overlay once per frame.
     *
     * `active` is the latest GameOverlayActivated_t state and `activations`
     * the number of activations Steam has reported so far (it only grows).
     */
    [[nodiscard]] SteamOverlayInputAction observe(bool active,
                                                  std::uint64_t activations) noexcept;
    /**
     * Forget the overlay, for example because the Steam runtime stopped and
     * no closing callback can arrive. Returns resume when input was suspended.
     */
    [[nodiscard]] SteamOverlayInputAction reset() noexcept;
    /** True from a suspend until the matching resume. */
    [[nodiscard]] bool suspended() const noexcept { return suspended_; }

private:
    std::uint64_t seen_activations_{};
    bool suspended_{};
};

} // namespace battlespades::platform
