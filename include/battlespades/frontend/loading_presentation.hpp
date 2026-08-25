#pragma once

#include "battlespades/frontend/loading_screen.hpp"
#include "battlespades/ui/draw_list.hpp"

namespace battlespades::frontend {

struct LoadingPresentationContext final {
    ui::PixelExtent window{800, 600};
    std::uint16_t background_opacity_per_mille{1'000U};
    /** Retail START glow pulses at 0.4 s; the caller supplies the phase. */
    bool start_glow{};
};

class BootLoadingPresentation final {
public:
    [[nodiscard]] ui::DrawList build(const BootLoadingSnapshot& snapshot,
                                     const LoadingPresentationContext& context = {}) const;
};

class MatchLoadingPresentation final {
public:
    [[nodiscard]] ui::DrawList build(const MatchLoadingSnapshot& snapshot,
                                     const LoadingPresentationContext& context = {}) const;
};

} // namespace battlespades::frontend
