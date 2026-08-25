#pragma once

#include <string_view>

#ifndef AOS_VERSION_STRING
#define AOS_VERSION_STRING "0.0.0"
#endif

#ifndef AOS_BUILD_PROFILE
#define AOS_BUILD_PROFILE "unknown"
#endif

namespace battlespades::core {

struct BuildInfo final {
    static constexpr std::string_view product_name{"BattleSpadesClient"};
    static constexpr std::string_view version{AOS_VERSION_STRING};
    static constexpr std::string_view profile{AOS_BUILD_PROFILE};
};

} // namespace battlespades::core
