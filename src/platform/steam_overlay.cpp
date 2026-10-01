#include "battlespades/platform/steam_overlay.hpp"

namespace battlespades::platform {

SteamOverlayInputAction SteamOverlayInputGate::observe(bool active,
                                                       std::uint64_t activations) noexcept {
    // A count that went backwards belongs to a restarted runtime; take it as
    // the new baseline rather than as billions of missed activations.
    const bool activated_since = activations > seen_activations_;
    seen_activations_ = activations;
    if (active) {
        if (suspended_) return SteamOverlayInputAction::none;
        suspended_ = true;
        return SteamOverlayInputAction::suspend;
    }
    if (suspended_) {
        suspended_ = false;
        return SteamOverlayInputAction::resume;
    }
    return activated_since ? SteamOverlayInputAction::flush : SteamOverlayInputAction::none;
}

SteamOverlayInputAction SteamOverlayInputGate::reset() noexcept {
    seen_activations_ = 0U;
    if (!suspended_) return SteamOverlayInputAction::none;
    suspended_ = false;
    return SteamOverlayInputAction::resume;
}

} // namespace battlespades::platform
