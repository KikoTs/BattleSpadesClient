#pragma once
#include "battlespades/core/utf8.hpp"
#include <array>
#include <string>
#include <string_view>
namespace battlespades::network {
inline std::string classic_decode_text(std::string_view value) {
    if (!value.empty() && static_cast<unsigned char>(value.front()) == 255) {
        auto utf8 = value.substr(1);
        auto valid = core::utf8_code_point_prefix(utf8, utf8.size());
        if (valid.size() == utf8.size())
            return valid;
    }
    constexpr std::array<unsigned, 128> cp437{
        0xc7,   0xfc,   0xe9,   0xe2,   0xe4,   0xe0,   0xe5,   0xe7,   0xea,   0xeb,   0xe8,
        0xef,   0xee,   0xec,   0xc4,   0xc5,   0xc9,   0xe6,   0xc6,   0xf4,   0xf6,   0xf2,
        0xfb,   0xf9,   0xff,   0xd6,   0xdc,   0xa2,   0xa3,   0xa5,   0x20a7, 0x192,  0xe1,
        0xed,   0xf3,   0xfa,   0xf1,   0xd1,   0xaa,   0xba,   0xbf,   0x2310, 0xac,   0xbd,
        0xbc,   0xa1,   0xab,   0xbb,   0x2591, 0x2592, 0x2593, 0x2502, 0x2524, 0x2561, 0x2562,
        0x2556, 0x2555, 0x2563, 0x2551, 0x2557, 0x255d, 0x255c, 0x255b, 0x2510, 0x2514, 0x2534,
        0x252c, 0x251c, 0x2500, 0x253c, 0x255e, 0x255f, 0x255a, 0x2554, 0x2569, 0x2566, 0x2560,
        0x2550, 0x256c, 0x2567, 0x2568, 0x2564, 0x2565, 0x2559, 0x2558, 0x2552, 0x2553, 0x256b,
        0x256a, 0x2518, 0x250c, 0x2588, 0x2584, 0x258c, 0x2590, 0x2580, 0x3b1,  0xdf,   0x393,
        0x3c0,  0x3a3,  0x3c3,  0xb5,   0x3c4,  0x3a6,  0x398,  0x3a9,  0x3b4,  0x221e, 0x3c6,
        0x3b5,  0x2229, 0x2261, 0xb1,   0x2265, 0x2264, 0x2320, 0x2321, 0xf7,   0x2248, 0xb0,
        0x2219, 0xb7,   0x221a, 0x207f, 0xb2,   0x25a0, 0xa0,
    };
    std::string text;
    for (unsigned char byte : value) {
        if (byte < 128) {
            if (byte >= 32 || byte == '\n')
                text.push_back(static_cast<char>(byte));
            continue;
        }
        const auto c = cp437[byte - 128];
        if (c < 2048) {
            text.push_back(static_cast<char>(192 | (c >> 6)));
            text.push_back(static_cast<char>(128 | (c & 63)));
        } else {
            text.push_back(static_cast<char>(224 | (c >> 12)));
            text.push_back(static_cast<char>(128 | ((c >> 6) & 63)));
            text.push_back(static_cast<char>(128 | (c & 63)));
        }
    }
    return text;
}
inline std::string classic_encode_text(std::string_view value, std::size_t max_bytes) {
    auto text = core::utf8_code_point_prefix(value, max_bytes);
    bool non_ascii{};
    for (unsigned char c : text)
        if (c >= 128)
            non_ascii = true;
    const auto budget = max_bytes - (non_ascii && max_bytes ? 1 : 0);
    if (text.size() > budget)
        text = core::utf8_code_point_prefix(std::string_view{text}.substr(0, budget), budget);
    if (non_ascii)
        text.insert(text.begin(), static_cast<char>(255));
    return text;
}
} // namespace battlespades::network
