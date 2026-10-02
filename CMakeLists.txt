cmake_minimum_required(VERSION 3.25)

project(
    BattleSpadesClient
    VERSION 0.2.1
    DESCRIPTION "Native Ace of Spades-compatible client"
    LANGUAGES CXX
)

# CMake versions are numeric. The public release label carries the beta tag
# and matches the BattleSpades server's VERSION format.
set(AOS_RELEASE_VERSION "${PROJECT_VERSION}-beta.2")

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(AosWarnings)
include(GNUInstallDirs)

if(WIN32 AND MSVC)
    # The portable ZIP must not rely on a Visual Studio or VC++ Redistributable
    # installation on the destination machine. CMake 3.31 predates the VS 18
    # v145 toolset and may otherwise reuse a cached VC143 redist directory, so
    # discover the CRT beside the exact cl.exe selected by the build wrapper.
    # InstallRequiredSystemLibraries still supplies the app-local Universal CRT
    # forwarders used by older/minimal Windows installations.
    set(CMAKE_INSTALL_SYSTEM_RUNTIME_DESTINATION "${CMAKE_INSTALL_BINDIR}")
    set(CMAKE_INSTALL_SYSTEM_RUNTIME_COMPONENT Runtime)
    set(CMAKE_INSTALL_UCRT_LIBRARIES TRUE)
    set(CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS_SKIP TRUE)
    include(InstallRequiredSystemLibraries)

    set(_aos_msvc_target_arch "${MSVC_CXX_ARCHITECTURE_ID}")
    if(NOT _aos_msvc_target_arch)
        set(_aos_msvc_target_arch "${CMAKE_SYSTEM_PROCESSOR}")
    endif()
    string(TOLOWER "${_aos_msvc_target_arch}" _aos_msvc_target_arch)
    if(_aos_msvc_target_arch MATCHES "^(x64|amd64|x86_64)$")
        set(_aos_msvc_redist_arch x64)
    elseif(_aos_msvc_target_arch MATCHES "^(win32|x86|i[3-6]86)$")
        set(_aos_msvc_redist_arch x86)
    elseif(_aos_msvc_target_arch STREQUAL "arm64")
        set(_aos_msvc_redist_arch arm64)
    elseif(_aos_msvc_target_arch STREQUAL "arm")
        set(_aos_msvc_redist_arch arm)
    elseif(CMAKE_SIZEOF_VOID_P EQUAL 8)
        # Ninja exposes MSVC_CXX_ARCHITECTURE_ID, but retain a safe fallback
        # for older generators targeting the currently supported x64 build.
        set(_aos_msvc_redist_arch x64)
    else()
        set(_aos_msvc_redist_arch x86)
    endif()
    get_filename_component(_aos_compiler_dir "${CMAKE_CXX_COMPILER}" DIRECTORY)
    get_filename_component(
        _aos_msvc_toolset_root
        "${_aos_compiler_dir}/../../.."
        ABSOLUTE
    )
    get_filename_component(_aos_msvc_toolset_version "${_aos_msvc_toolset_root}" NAME)
    get_filename_component(
        _aos_vc_root
        "${_aos_msvc_toolset_root}/../../.."
        ABSOLUTE
    )
    file(
        GLOB _aos_compiler_crt
        LIST_DIRECTORIES FALSE
        "${_aos_vc_root}/Redist/MSVC/${_aos_msvc_toolset_version}/${_aos_msvc_redist_arch}/Microsoft.VC*.CRT/*.dll"
    )
    # Visual Studio 2022 numbers its redist folder apart from the toolset
    # (Tools/MSVC/14.44.35207 vs Redist/MSVC/14.44.35112). The developer
    # environment that selected this cl.exe names the matching folder; accept
    # it only when it belongs to the same Visual Studio installation.
    if(NOT _aos_compiler_crt AND DEFINED ENV{VCToolsRedistDir})
        file(TO_CMAKE_PATH "$ENV{VCToolsRedistDir}" _aos_env_redist_dir)
        get_filename_component(_aos_env_redist_dir "${_aos_env_redist_dir}" ABSOLUTE)
        string(TOLOWER "${_aos_env_redist_dir}/" _aos_env_redist_prefix)
        string(TOLOWER "${_aos_vc_root}/Redist/MSVC/" _aos_vc_redist_prefix)
        string(FIND "${_aos_env_redist_prefix}" "${_aos_vc_redist_prefix}" _aos_redist_match)
        if(_aos_redist_match EQUAL 0)
            file(
                GLOB _aos_compiler_crt
                LIST_DIRECTORIES FALSE
                "${_aos_env_redist_dir}/${_aos_msvc_redist_arch}/Microsoft.VC*.CRT/*.dll"
            )
        endif()
    endif()
    if(NOT _aos_compiler_crt)
        message(
            FATAL_ERROR
            "The redistributable CRT matching ${CMAKE_CXX_COMPILER} was not found"
        )
    endif()

    get_filename_component(
        _aos_windows_kits_root
        "[HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows Kits\\Installed Roots;KitsRoot10]"
        ABSOLUTE
    )
    file(
        GLOB _aos_ucrt_directories
        LIST_DIRECTORIES TRUE
        "${_aos_windows_kits_root}/Redist/*/ucrt/DLLs/${_aos_msvc_redist_arch}"
    )
    set(
        _aos_ucrt_fallback_directory
        "${_aos_windows_kits_root}/Redist/ucrt/DLLs/${_aos_msvc_redist_arch}"
    )
    list(
        FILTER _aos_ucrt_directories
        EXCLUDE REGEX "[/\\\\]Redist[/\\\\]ucrt[/\\\\]"
    )
    list(SORT _aos_ucrt_directories COMPARE NATURAL ORDER DESCENDING)
    if(IS_DIRECTORY "${_aos_ucrt_fallback_directory}")
        list(APPEND _aos_ucrt_directories "${_aos_ucrt_fallback_directory}")
    endif()
    set(_aos_ucrt_runtime)
    foreach(_aos_ucrt_directory IN LISTS _aos_ucrt_directories)
        if(EXISTS "${_aos_ucrt_directory}/ucrtbase.dll")
            file(
                GLOB _aos_ucrt_runtime
                LIST_DIRECTORIES FALSE
                "${_aos_ucrt_directory}/*.dll"
            )
            break()
        endif()
    endforeach()
    if(NOT _aos_ucrt_runtime)
        message(
            FATAL_ERROR
            "The app-local UCRT for ${_aos_msvc_redist_arch} was not found under "
            "${_aos_windows_kits_root}"
        )
    endif()

    # Remove any compiler CRT or UCRT paths recovered through a persistent
    # CMake cache, then add exactly the active toolset and latest SDK set.
    list(
        FILTER CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS
        EXCLUDE REGEX "[/\\\\]Microsoft\\.VC[0-9]+\\.CRT[/\\\\]"
    )
    list(
        FILTER CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS
        EXCLUDE REGEX "[/\\\\]ucrt[/\\\\]DLLs[/\\\\]"
    )
    list(
        APPEND CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS
        ${_aos_compiler_crt}
        ${_aos_ucrt_runtime}
    )
    list(REMOVE_DUPLICATES CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS)
    install(
        PROGRAMS ${CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS}
        DESTINATION "${CMAKE_INSTALL_SYSTEM_RUNTIME_DESTINATION}"
        COMPONENT Runtime
    )
endif()

option(AOS_BUILD_TESTS "Build the native core test suite" ON)
option(AOS_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" ON)
option(AOS_ENABLE_STEAM_NETWORKING "Build peer-to-peer transport over Valve's relay network" ON)
option(
    AOS_ENABLE_NATIVE_BACKENDS
    "Build the SDL3/bgfx/OpenAL graphical client backends"
    OFF
)
option(
    AOS_WINDOWS_RELEASE_GUI
    "Build the native MSVC Release executable without a persistent console window"
    ON
)

set(AOS_BUILD_PROFILE "dev" CACHE STRING "Build profile: debug, dev, or release")
set_property(CACHE AOS_BUILD_PROFILE PROPERTY STRINGS debug dev release)
set(
    AOS_ASSET_ROOT
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/original"
    CACHE PATH
    "Read-only preserved asset root"
)
set(
    AOS_BUNDLED_SERVER_ROOT
    ""
    CACHE PATH
    "Optional read-only portable BattleSpades bundle installed beside the client"
)

if(NOT AOS_BUILD_PROFILE MATCHES "^(debug|dev|release)$")
    message(FATAL_ERROR "AOS_BUILD_PROFILE must be debug, dev, or release")
endif()

set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_library(aos_build_options INTERFACE)
add_library(BattleSpades::BuildOptions ALIAS aos_build_options)

target_compile_features(aos_build_options INTERFACE cxx_std_20)
# Movement prediction replays the server's arithmetic and must round the same
# way. Clang and GCC may fuse a*b+c into one instruction (the default on arm64),
# which rounds differently: on Apple Silicon a predicted flight ended 1.3 blocks
# from the authoritative one after 360 frames.
target_compile_options(
    aos_build_options
    INTERFACE
        $<$<CXX_COMPILER_ID:Clang,AppleClang,GNU>:-ffp-contract=off>
)
target_compile_definitions(
    aos_build_options
    INTERFACE
        AOS_VERSION_STRING="${AOS_RELEASE_VERSION}"
        AOS_BUILD_PROFILE="${AOS_BUILD_PROFILE}"
        $<$<STREQUAL:${AOS_BUILD_PROFILE},release>:AOS_ENABLE_DEVELOPER_TOOLS=0>
        $<$<NOT:$<STREQUAL:${AOS_BUILD_PROFILE},release>>:AOS_ENABLE_DEVELOPER_TOOLS=1>
        $<$<CONFIG:Debug>:AOS_ENABLE_ASSERTIONS=1>
        $<$<CONFIG:RelWithDebInfo>:AOS_ENABLE_ASSERTIONS=1>
)

add_subdirectory(src)

if(AOS_BUNDLED_SERVER_ROOT)
    if(WIN32)
        set(_aos_server_executable "BattleSpades.exe")
    else()
        set(_aos_server_executable "BattleSpades")
    endif()
    if(NOT EXISTS "${AOS_BUNDLED_SERVER_ROOT}/${_aos_server_executable}" OR
       NOT IS_DIRECTORY "${AOS_BUNDLED_SERVER_ROOT}/_internal" OR
       NOT IS_DIRECTORY "${AOS_BUNDLED_SERVER_ROOT}/maps")
        message(
            FATAL_ERROR
            "AOS_BUNDLED_SERVER_ROOT is not a complete portable BattleSpades bundle: "
            "${AOS_BUNDLED_SERVER_ROOT}"
        )
    endif()
    # Copy only during install/package. The server repository remains a
    # read-only release input and is never configured, patched or generated.
    install(
        DIRECTORY "${AOS_BUNDLED_SERVER_ROOT}/"
        DESTINATION "${CMAKE_INSTALL_BINDIR}/server"
        USE_SOURCE_PERMISSIONS
        COMPONENT Runtime
    )
endif()

if(AOS_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# Auto-updating launcher and installer Steam helper (docs/INSTALLER_AND_UPDATER.md).
if(AOS_ENABLE_NATIVE_BACKENDS)
    add_subdirectory(src/updater)
endif()

find_package(Python3 3.10 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
    add_custom_target(
        verify-assets
        COMMAND
            "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/assets.py"
            verify
            --destination
            "${AOS_ASSET_ROOT}"
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        COMMENT "Verifying preserved asset catalog"
        VERBATIM
    )
else()
    message(
        STATUS
        "Python 3.10+ was not found; the optional verify-assets target is disabled"
    )
endif()

install(
    TARGETS BattleSpadesClient
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
)

if(AOS_ENABLE_NATIVE_BACKENDS)
    install(
        TARGETS BattleSpadesAssetInstaller
        RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
        COMPONENT Runtime
    )
    # Retail assets are deliberately not redistributed. The adjacent manifest
    # lets the packaged installer validate and import them from the player's
    # existing Ace of Spades installation on first launch.
    install(
        FILES "${CMAKE_CURRENT_SOURCE_DIR}/assets/catalog/original-assets.json"
        DESTINATION "${CMAKE_INSTALL_BINDIR}"
        RENAME "asset-manifest.json"
        COMPONENT Runtime
    )
    # Project-owned additions remain separate because an asset repair replaces
    # assets/original atomically with the verified retail content.
    install(
        DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/assets/client/"
        DESTINATION "${CMAKE_INSTALL_BINDIR}/assets/client"
        COMPONENT Runtime
    )
    install(
        DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/src/render/shaders/bin/"
        DESTINATION "${CMAKE_INSTALL_BINDIR}/shaders"
        COMPONENT Runtime
    )
    install(
        FILES "${CMAKE_CURRENT_SOURCE_DIR}/config/ui-layout.json"
        DESTINATION "${CMAKE_INSTALL_BINDIR}"
        COMPONENT Runtime
    )
    install(
        DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/config/localization/"
        DESTINATION "${CMAKE_INSTALL_BINDIR}/localization"
        COMPONENT Runtime
    )

    if(WIN32)
        install(
            PROGRAMS "${AOS_STEAM_BRIDGE_EXECUTABLE}"
            DESTINATION "${CMAKE_INSTALL_BINDIR}"
            COMPONENT Runtime
        )
        # The executable's post-build step has already materialized the full
        # transitive DLL set here. Installing from that verified directory is
        # more reliable than re-evaluating TARGET_RUNTIME_DLLS during install;
        # the latter omitted HarfBuzz with the pinned vcpkg target graph.
        install(
            DIRECTORY "$<TARGET_FILE_DIR:BattleSpadesClient>/"
            DESTINATION "${CMAKE_INSTALL_BINDIR}"
            COMPONENT Runtime
            FILES_MATCHING PATTERN "*.dll"
        )

        # Steam's original application entry point is named aos.exe.  Install
        # the post-build compatibility alias as well as BattleSpadesClient.exe
        # so either launch path uses the identical client build.
        install(
            PROGRAMS "$<TARGET_FILE_DIR:BattleSpadesClient>/aos.exe"
            DESTINATION "${CMAKE_INSTALL_BINDIR}"
            COMPONENT Runtime
        )
    endif()
endif()

# AGPL-3.0-or-later: every package carries the license, its summary (with the
# Steamworks section 7 permission) and the third-party notices. They go beside
# the executable because the macOS bundle is built from bin/ alone.
install(
    FILES
        "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE"
        "${CMAKE_CURRENT_SOURCE_DIR}/LICENSING.md"
        "${CMAKE_CURRENT_SOURCE_DIR}/THIRD_PARTY_NOTICES.md"
    DESTINATION "${CMAKE_INSTALL_BINDIR}"
    COMPONENT Runtime
)

set(CPACK_PACKAGE_NAME "BattleSpadesClient")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
set(CPACK_PACKAGE_VENDOR "BattleSpades")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Portable native BattleSpades client")
set(
    CPACK_PACKAGE_FILE_NAME
    "BattleSpadesClient-${AOS_RELEASE_VERSION}-${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}"
)
set(CPACK_GENERATOR "ZIP")
set(CPACK_PACKAGE_CHECKSUM "SHA256")
include(CPack)
