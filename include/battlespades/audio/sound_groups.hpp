#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace battlespades::audio {

/**
 * Expands a retail sound group identifier into the exact OGG stems it names.
 *
 * Retail writes a numbered family as `des_imp_small_001-004`, meaning the four
 * files `des_imp_small_001` through `des_imp_small_004`, one of which is
 * chosen at random per playback. A name without that suffix denotes a single
 * file and is returned unchanged. An empty group means retail is deliberately
 * silent and yields no stems.
 *
 * Header-only and free of any OpenAL dependency so the catalog-to-asset
 * mapping can be verified without an audio device.
 */
[[nodiscard]] inline std::vector<std::string> sound_group_stems(std::string_view group) {
    if (group.empty()) {
        return {};
    }
    const auto dash = group.rfind('-');
    if (dash == std::string_view::npos || dash < 3U || dash + 4U != group.size()) {
        return {std::string{group}};
    }
    const auto first_text = group.substr(dash - 3U, 3U);
    const auto last_text = group.substr(dash + 1U, 3U);
    const auto digits = [](std::string_view text) {
        return std::ranges::all_of(text, [](char value) { return value >= '0' && value <= '9'; });
    };
    if (!digits(first_text) || !digits(last_text)) {
        return {std::string{group}};
    }
    const auto parse = [](std::string_view text) {
        return static_cast<unsigned>((text[0U] - '0') * 100 +
                                     (text[1U] - '0') * 10 + (text[2U] - '0'));
    };
    const unsigned first = parse(first_text);
    const unsigned last = parse(last_text);
    if (first == 0U || last < first || last - first > 32U) {
        return {std::string{group}};
    }
    const std::string prefix{group.substr(0U, dash - 3U)};
    std::vector<std::string> result;
    result.reserve(last - first + 1U);
    for (unsigned index = first; index <= last; ++index) {
        std::string number = std::to_string(index);
        number.insert(number.begin(), 3U - number.size(), '0');
        result.push_back(prefix + number);
    }
    return result;
}

/**
 * Reproduces MediaManager.play's authored random-pitch conversion.
 *
 * Retail stores minimum/maximum pitch in semitones at cue indices 3 and 4.
 * When they differ, Python 2 truncates `(semitones / 12) * 1000` toward zero,
 * chooses an integer from that inclusive range, then converts the result back
 * to a frequency ratio. Passing the random draw in keeps this helper pure and
 * lets the OpenAL-free parity tests pin the otherwise easy-to-miss rounding.
 */
[[nodiscard]] inline float retail_sound_pitch_ratio(float minimum_semitones,
                                                    float maximum_semitones,
                                                    std::uint32_t random_draw) noexcept {
    if (!std::isfinite(minimum_semitones) || !std::isfinite(maximum_semitones)) {
        return 1.0F;
    }
    if (minimum_semitones > maximum_semitones) {
        std::swap(minimum_semitones, maximum_semitones);
    }

    float selected_semitones = minimum_semitones;
    if (minimum_semitones != maximum_semitones) {
        const auto minimum = static_cast<std::int32_t>(minimum_semitones / 12.0F * 1000.0F);
        const auto maximum = static_cast<std::int32_t>(maximum_semitones / 12.0F * 1000.0F);
        if (maximum >= minimum) {
            const auto span = static_cast<std::uint32_t>(maximum - minimum) + 1U;
            const auto selected = minimum + static_cast<std::int32_t>(random_draw % span);
            selected_semitones = static_cast<float>(selected) / 1000.0F * 12.0F;
        }
    }
    return std::pow(2.0F, selected_semitones / 12.0F);
}

} // namespace battlespades::audio
