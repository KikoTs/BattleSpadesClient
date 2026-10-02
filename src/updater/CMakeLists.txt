# BattleSpadesLauncher (auto-updater + Steam %command% entry point) and
# BattleSpadesSetupHelper (Steam detection/registration for the installer).
# See docs/INSTALLER_AND_UPDATER.md.
#
# Both executables link the static CRT and system DLLs only: the launcher
# must be able to replace every DLL of the install folder, and the helper is
# run from the installer's temp folder before any runtime DLL is installed.

find_package(nlohmann_json CONFIG QUIET)
if(NOT nlohmann_json_FOUND)
    message(STATUS "nlohmann_json was not found; the BattleSpades launcher/updater is disabled")
    return()
endif()

set(_aos_static_crt "MultiThreaded$<$<CONFIG:Debug>:Debug>")

include("${CMAKE_CURRENT_SOURCE_DIR}/core_sources.cmake")
add_library(aos_updater_core STATIC ${AOS_UPDATER_CORE_SOURCES})
add_library(BattleSpades::UpdaterCore ALIAS aos_updater_core)
target_include_directories(aos_updater_core PUBLIC "${PROJECT_SOURCE_DIR}/include")
target_link_libraries(
    aos_updater_core
    PUBLIC
        BattleSpades::BuildOptions
        nlohmann_json::nlohmann_json
)
set_property(TARGET aos_updater_core PROPERTY MSVC_RUNTIME_LIBRARY "${_aos_static_crt}")
aos_enable_strict_warnings(aos_updater_core)

# Our own redistributable asset packs (assetsient) are versioned on their
# own so the launcher can update them separately from the client.
set(AOS_ASSETS_VERSION "${AOS_RELEASE_VERSION}" CACHE STRING "Version of the bundled assets/client packs")
# Network protocol of this client; the launcher compares it with the bundled
# server's to decide whether hosting works (playing never depends on it).
set(AOS_NETWORK_PROTOCOL "168" CACHE STRING "Network protocol spoken by this client build")

configure_file(
    battlespades-version.json.in
    "${CMAKE_CURRENT_BINARY_DIR}/battlespades-version.json"
    @ONLY
)
# The launcher compares this against the newest GitHub release; every update
# package carries its own copy, so it is replaced together with the client.
install(
    FILES "${CMAKE_CURRENT_BINARY_DIR}/battlespades-version.json"
    DESTINATION "${CMAKE_INSTALL_BINDIR}"
    COMPONENT Runtime
)

if(WIN32)
    add_executable(
        BattleSpadesLauncher
        WIN32
            windows/launcher_main.cpp
            windows/update_session.cpp
            windows/win_util.cpp
            windows/launcher.rc
    )
    add_executable(
        BattleSpadesSetupHelper
            windows/setup_helper_main.cpp
            windows/win_util.cpp
            windows/setup_helper.rc
    )
    foreach(_aos_updater_exe IN ITEMS BattleSpadesLauncher BattleSpadesSetupHelper)
        target_link_libraries(
            ${_aos_updater_exe}
            PRIVATE
                BattleSpades::UpdaterCore
                winhttp
                comctl32
                shell32
                ole32
                user32
                gdi32
                advapi32
        )
        target_compile_definitions(${_aos_updater_exe} PRIVATE UNICODE _UNICODE)
        set_property(TARGET ${_aos_updater_exe} PROPERTY MSVC_RUNTIME_LIBRARY "${_aos_static_crt}")
        if(MSVC)
            # launcher.rc embeds the asInvoker manifest; suppress the linker's own.
            target_link_options(${_aos_updater_exe} PRIVATE /MANIFEST:NO)
        endif()
        aos_enable_strict_warnings(${_aos_updater_exe})
    endforeach()

    install(
        TARGETS BattleSpadesLauncher BattleSpadesSetupHelper
        RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
        COMPONENT Runtime
    )
endif()

if(AOS_BUILD_TESTS)
    add_subdirectory("${PROJECT_SOURCE_DIR}/tests/updater" "${CMAKE_CURRENT_BINARY_DIR}/tests")
endif()
