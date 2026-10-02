# Platform-independent launcher/updater core (manifest, SHA-256, ZIP, Steam
# library files, ...). Built twice: aos_updater_core (static CRT, for the
# Windows launcher and setup helper) and aos_updater_portable (default CRT,
# for BattleSpadesAssetInstaller and the client on every OS).
set(
    AOS_UPDATER_CORE_SOURCES
    binary_vdf.cpp
    file_util.cpp
    launch_flow.cpp
    launcher_args.cpp
    release_manifest.cpp
    semver.cpp
    sha256.cpp
    steam_library.cpp
    steam_registration.cpp
    text_vdf.cpp
    update_apply.cpp
    zip_extract.cpp
    update_manifest.cpp
    update_plan.cpp
    updater_config.cpp
)
list(TRANSFORM AOS_UPDATER_CORE_SOURCES PREPEND "${CMAKE_CURRENT_LIST_DIR}/")
