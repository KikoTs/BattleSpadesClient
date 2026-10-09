#pragma once

#include <array>
#include <cstdint>
#include <string_view>

namespace battlespades::frontend {

enum class FallbackMusicAction { none, start, stop };

/** Owns only optional client music, never a server or menu track. */
class FallbackMusic final {
public:
    [[nodiscard]] FallbackMusicAction
    update(bool enabled, bool in_match, bool server_controlled) noexcept {
        if (server_controlled) {
            // A PlayMusic OR StopMusic cue transfers ownership to the server.
            relinquish();
            return FallbackMusicAction::none;
        }
        const bool wanted = enabled && in_match;
        if (wanted == active_)
            return FallbackMusicAction::none;
        active_ = wanted;
        return wanted ? FallbackMusicAction::start : FallbackMusicAction::stop;
    }

    void relinquish() noexcept {
        active_ = false;
    }

    [[nodiscard]] std::string_view choose_track(std::uint32_t roll) noexcept {
        static constexpr std::array<std::string_view, 4> tracks{"last_man_standing_001",
                                                                "last_man_standing_002",
                                                                "last_man_standing_003",
                                                                "last_man_standing_004"};
        auto index = roll % static_cast<std::uint32_t>(tracks.size());
        if (index == last_track_)
            index = (index + 1U) % static_cast<std::uint32_t>(tracks.size());
        last_track_ = index;
        return tracks[index];
    }

private:
    bool active_{};
    std::uint32_t last_track_{4U};
};

} // namespace battlespades::frontend
