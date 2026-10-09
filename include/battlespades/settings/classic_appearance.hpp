#pragma once

#include <array>
#include <string_view>

namespace battlespades::settings {

struct ClassicSkyOption final {
    std::string_view id;
    std::string_view label;
    std::string_view skydome;
};

inline constexpr std::array classic_sky_options{
    ClassicSkyOption{"server", "CLASSIC_SKY_DEFAULT", ""},
    ClassicSkyOption{"random", "CLASSIC_SKY_RANDOM", ""},
    ClassicSkyOption{"clear", "CLASSIC_SKY_CLEAR", "User_Grassland.txt"},
    ClassicSkyOption{"overcast", "CLASSIC_SKY_OVERCAST", "Classic_B.txt"},
    ClassicSkyOption{"desert", "CLASSIC_SKY_DESERT", "Egypt.txt"},
    ClassicSkyOption{"sunset", "CLASSIC_SKY_SUNSET", "Frontier.txt"},
    ClassicSkyOption{"night", "CLASSIC_SKY_NIGHT", "SecretBase_Night.txt"},
    ClassicSkyOption{"snow", "CLASSIC_SKY_SNOW", "ArcticBase.txt"},
};

[[nodiscard]] constexpr const ClassicSkyOption* classic_sky_option(std::string_view id) noexcept {
    for (const auto& option : classic_sky_options) {
        if (option.id == id) return &option;
    }
    return nullptr;
}

inline constexpr std::array<std::string_view, 4U> classic_fog_options{"gray", "server", "sky", "custom"};
inline constexpr std::array<std::string_view, 4U> classic_fog_labels{
    "CLASSIC_FOG_GRAY", "CLASSIC_FOG_SERVER", "CLASSIC_FOG_SKY", "CLASSIC_FOG_CUSTOM"};

[[nodiscard]] constexpr bool valid_classic_fog(std::string_view id) noexcept {
    for (const auto option : classic_fog_options) {
        if (option == id) return true;
    }
    return false;
}

} // namespace battlespades::settings
