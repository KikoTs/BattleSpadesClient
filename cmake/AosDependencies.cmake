# World/frontend code also uses JSON in builds without graphics or tests.
find_package(nlohmann_json CONFIG QUIET)
if(NOT TARGET nlohmann_json::nlohmann_json)
    if(NOT CMAKE_SYSTEM_NAME STREQUAL "Haiku")
        find_package(nlohmann_json CONFIG REQUIRED)
    else()
        # Some Haiku packages install headers/config but omit the namespaced target.
        find_path(AOS_JSON_INCLUDE_DIR nlohmann/json.hpp REQUIRED)
        add_library(nlohmann_json::nlohmann_json INTERFACE IMPORTED GLOBAL)
        set_target_properties(nlohmann_json::nlohmann_json PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES "${AOS_JSON_INCLUDE_DIR}")
    endif()
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Haiku" AND AOS_ENABLE_NATIVE_BACKENDS)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(AOS_SODIUM REQUIRED IMPORTED_TARGET GLOBAL libsodium)
    pkg_check_modules(AOS_ENET REQUIRED IMPORTED_TARGET GLOBAL libenet)
    add_library(unofficial-sodium::sodium ALIAS PkgConfig::AOS_SODIUM)
    add_library(unofficial::enet::enet ALIAS PkgConfig::AOS_ENET)
endif()
