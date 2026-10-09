#pragma once

#include <cstddef>
#include <span>
#include <stdexcept>
#include <vector>

namespace battlespades::audio {

/**
 * OpenAL 1.1 spatializes mono buffers only. Keep authored stereo for local/UI
 * playback and prepare this second buffer once for a positioned world emitter.
 * An integer accumulator prevents overflow, including two full-scale samples.
 */
[[nodiscard]] inline std::vector<short> spatial_mono_pcm(std::span<const short> stereo) {
    if (stereo.size() % 2U != 0U) {
        throw std::invalid_argument{"incomplete stereo PCM frame"};
    }
    std::vector<short> mono(stereo.size() / 2U);
    for (std::size_t frame{}; frame < mono.size(); ++frame) {
        const int sum = static_cast<int>(stereo[frame * 2U]) +
                        static_cast<int>(stereo[frame * 2U + 1U]);
        mono[frame] = static_cast<short>(sum / 2);
    }
    return mono;
}

} // namespace battlespades::audio
