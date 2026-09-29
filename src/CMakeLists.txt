add_library(
    aos_core
    STATIC
        core/application.cpp
        core/command_line.cpp
        core/diagnostics.cpp
        core/resource_paths.cpp
        core/utf8.cpp
)
add_library(BattleSpades::Core ALIAS aos_core)

target_include_directories(
    aos_core
    PUBLIC
        "${PROJECT_SOURCE_DIR}/include"
)
target_link_libraries(aos_core PUBLIC BattleSpades::BuildOptions)
aos_enable_strict_warnings(aos_core)

add_library(
    aos_settings
    STATIC
        settings/client_settings.cpp
        settings/retail_key_names.cpp
        settings/settings_session.cpp
        settings/settings_store.cpp
        # Tier resolution is pure settings->renderer-knobs and must stay
        # testable without a GPU, so it lives here rather than in the
        # native-only bgfx target.
        render/quality_profile.cpp
)
add_library(BattleSpades::Settings ALIAS aos_settings)

target_include_directories(
    aos_settings
    PUBLIC
        "${PROJECT_SOURCE_DIR}/include"
)
target_link_libraries(aos_settings PUBLIC BattleSpades::BuildOptions)
aos_enable_strict_warnings(aos_settings)

add_library(
    aos_assets
    STATIC
        assets/preload_service.cpp
)
add_library(BattleSpades::Assets ALIAS aos_assets)
target_include_directories(
    aos_assets
    PUBLIC
        "${PROJECT_SOURCE_DIR}/include"
)
target_link_libraries(aos_assets PUBLIC BattleSpades::BuildOptions)
aos_enable_strict_warnings(aos_assets)

add_subdirectory("${PROJECT_SOURCE_DIR}/third_party/angelscript" "${PROJECT_BINARY_DIR}/third_party/angelscript")
add_library(
    aos_world
    STATIC
        world/vxl_map.cpp
        world/chunk_mesher.cpp
        world/class_catalog.generated.cpp
        world/class_selection.cpp
        world/class_models.cpp
        world/disguise_models.cpp
        world/gameplay_debug_lab.cpp
        world/kv6_model.cpp
        world/cosmetic_preview.cpp
        world/prefab_placement.cpp
        world/ugc_prefab_control.cpp
        world/player_inventory.cpp
        world/jetpack_death.cpp
        world/player_movement.cpp
        world/retail_inventory.cpp
        world/retail_character_pose.cpp
        world/retail_view_model.cpp
        world/weapon_catalog.generated.cpp
        world/weapon_secondary.cpp
        world/weapon_zoom.cpp
        world/weapon_models.cpp
        world/class_voice.cpp
        world/class_voice.generated.cpp
        world/entity_catalog.cpp
        world/entity_catalog.generated.cpp
        world/local_entity.cpp
        world/footstep_audio.cpp
        world/remote_character_audio.cpp
        world/weapon_fire_sound.cpp
        world/weapon_presentation.cpp
        world/scripted_weapon.cpp
        world/weapon_variants.cpp
        world/weapon_runtime.cpp
        world/weapon_state.cpp
        world/tutorial_lessons.cpp
        world/tutorial_session.cpp
        world/tutorial_bootstrap.cpp
        world/terrain_effects.cpp
        world/replicated_shot.cpp
        world/particle_system.cpp
        world/particle_effects.cpp
        world/projectile_presentation.cpp
        world/map_atmosphere.cpp
        world/skylight_map.cpp
        world/emissive_set.cpp
        world/static_light_field.cpp
        world/retail_effects.cpp
        world/map_catalog.cpp
        world/map_spawn.cpp
        world/minimap_overview.cpp
        world/emissive_volume.cpp
        world/voxel_collapse.cpp
        world/voxel_raycast.cpp
        world/sniper_laser.cpp
        world/block_placement.cpp
        world/retail_blast.cpp
)
add_library(BattleSpades::World ALIAS aos_world)
target_include_directories(aos_world PUBLIC "${PROJECT_SOURCE_DIR}/include")
target_link_libraries(aos_world PUBLIC BattleSpades::BuildOptions PRIVATE nlohmann_json::nlohmann_json aos_angelscript)
aos_enable_strict_warnings(aos_world)

add_library(
    aos_protocol
    STATIC
        network/protocol168_players.cpp
        network/protocol168_reconciliation.cpp
        network/protocol168_runtime.cpp
        network/protocol168_terrain.cpp
        network/protocol168_tool_actions.cpp
        network/protocol168_ugc.cpp
        network/protocol168_weapon_action_adapter.cpp
        network/protocol168_weapons.cpp
)
add_library(BattleSpades::Protocol ALIAS aos_protocol)
target_include_directories(aos_protocol PUBLIC "${PROJECT_SOURCE_DIR}/include")
target_link_libraries(aos_protocol PUBLIC BattleSpades::World BattleSpades::BuildOptions)
aos_enable_strict_warnings(aos_protocol)

add_library(
    aos_ui
    STATIC
        ui/design_canvas.cpp
        ui/draw_list.cpp
        ui/focus_navigator.cpp
        ui/geometry.cpp
        ui/draw_list_transform.cpp
        ui/screen_stack.cpp
)
add_library(BattleSpades::Ui ALIAS aos_ui)

target_include_directories(
    aos_ui
    PUBLIC
        "${PROJECT_SOURCE_DIR}/include"
)
target_link_libraries(aos_ui PUBLIC BattleSpades::BuildOptions)
aos_enable_strict_warnings(aos_ui)

add_library(
    aos_frontend
    STATIC
        frontend/create_match_menu.cpp
        frontend/create_match_presentation.cpp
        frontend/custom_match_menu.cpp
        frontend/custom_match_presentation.cpp
        frontend/class_selection_menu.cpp
        frontend/class_selection_presentation.cpp
        frontend/change_team_menu.cpp
        frontend/kick_vote_menu.cpp
        frontend/death_camera.cpp
        frontend/favorite_server_store.cpp
        frontend/frontend_controller.cpp
        frontend/frontend_navigation.cpp
        frontend/frontend_shell.cpp
        frontend/friends_lobby_menu.cpp
        frontend/friends_lobby_presentation.cpp
        frontend/game_hud.cpp
        frontend/hud_layout.cpp
        frontend/identity_menu.cpp
        frontend/identity_presentation.cpp
        frontend/gameplay_debug_presentation.cpp
        frontend/join_match_menu.cpp
        frontend/join_match_presentation.cpp
        frontend/leaderboard_menu.cpp
        frontend/leaderboard_presentation.cpp
        frontend/loading_presentation.cpp
        frontend/loading_screen.cpp
        frontend/localization_catalog.cpp
        frontend/main_menu.cpp
        frontend/main_menu_presentation.cpp
        frontend/match_overlays.cpp
        frontend/retail_input_rules.cpp
        frontend/retail_hud_rules.cpp
        frontend/objective_indicator.cpp
        frontend/parity_debug_menu.cpp
        frontend/parity_debug_presentation.cpp
        frontend/pause_menu.cpp
        frontend/player_progression.cpp
        frontend/player_profile_menu.cpp
        frontend/inventory_menu.cpp
        frontend/player_profile_presentation.cpp
        frontend/player_name_projection.cpp
        frontend/quick_play_menu.cpp
        frontend/quick_play_presentation.cpp
        frontend/resolution_confirmation.cpp
        frontend/runtime_vfx_adapter.cpp
        frontend/settings_menu.cpp
        frontend/settings_presentation.cpp
        frontend/ugc_prefab_catalog.cpp
        frontend/ugc_loadout_menu.cpp
        frontend/ugc_loadout_presentation.cpp
        frontend/ugc_status_presentation.cpp
        frontend/ugc_select_menu.cpp
        frontend/ugc_select_presentation.cpp
        frontend/ugc_publish_menu.cpp
        frontend/ugc_publish_presentation.cpp
        frontend/ugc_project_repository.cpp
        frontend/ugc_editor_menu.cpp
        frontend/ugc_editor_presentation.cpp
        frontend/ui_layout_editor.cpp
        frontend/retail_ui_catalog.generated.cpp
)
add_library(BattleSpades::Frontend ALIAS aos_frontend)

target_include_directories(
    aos_frontend
    PUBLIC
        "${PROJECT_SOURCE_DIR}/include"
)
target_link_libraries(
    aos_frontend
    PUBLIC
        BattleSpades::Ui
        BattleSpades::Assets
        BattleSpades::Core
        BattleSpades::Settings
        BattleSpades::World
        BattleSpades::Protocol
        BattleSpades::BuildOptions
    PRIVATE
        nlohmann_json::nlohmann_json
)
aos_enable_strict_warnings(aos_frontend)

add_library(
    aos_ports
    INTERFACE
        "${PROJECT_SOURCE_DIR}/include/battlespades/audio/audio_port.hpp"
        "${PROJECT_SOURCE_DIR}/include/battlespades/network/transport_port.hpp"
        "${PROJECT_SOURCE_DIR}/include/battlespades/platform/window_port.hpp"
        "${PROJECT_SOURCE_DIR}/include/battlespades/render/renderer_port.hpp"
)
add_library(BattleSpades::Ports ALIAS aos_ports)
target_link_libraries(aos_ports INTERFACE BattleSpades::Core)

add_library(
    aos_headless
    STATIC
        headless/headless_module.cpp
)
add_library(BattleSpades::Headless ALIAS aos_headless)

target_include_directories(
    aos_headless
    PUBLIC
        "${PROJECT_SOURCE_DIR}/include"
)
target_link_libraries(aos_headless PUBLIC BattleSpades::Core)
aos_enable_strict_warnings(aos_headless)

add_executable(
    aos_emissive_vantage
    "${PROJECT_SOURCE_DIR}/tools/emissive_vantage.cpp"
)
target_link_libraries(aos_emissive_vantage PRIVATE BattleSpades::World)
aos_enable_strict_warnings(aos_emissive_vantage)

if(AOS_ENABLE_NATIVE_BACKENDS)
    find_package(SDL3 CONFIG REQUIRED)
    find_package(bgfx CONFIG REQUIRED)
    find_package(Freetype REQUIRED)
    find_package(harfbuzz CONFIG REQUIRED)
    find_package(CURL REQUIRED)
    find_package(nlohmann_json CONFIG REQUIRED)
    find_package(unofficial-sodium CONFIG REQUIRED)
    # Use the native OpenAL framework on macOS. Other platforms receive
    # OpenAL Soft from vcpkg, and CMake's module exposes the same target.
    find_package(OpenAL REQUIRED)
    find_package(Stb REQUIRED)
    find_package(ZLIB REQUIRED)
    find_package(unofficial-enet CONFIG REQUIRED)

    # Native HTML/CSS-style menus share the existing renderer and asset tree.
    # Pin the release archive; no browser process or runtime download is used.
    include(FetchContent)
    set(RMLUI_SAMPLES OFF CACHE BOOL "" FORCE)
    set(RMLUI_LUA_BINDINGS OFF CACHE BOOL "" FORCE)
    set(RMLUI_FONT_ENGINE "freetype" CACHE STRING "" FORCE)
    FetchContent_Declare(rmlui
        URL https://codeload.github.com/mikke89/RmlUi/tar.gz/refs/tags/6.3
        URL_HASH SHA256=d977298bb6147610e5984d5db85ddf284020d655a8713913f6982074f1dbdede
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        # Its headers are not ours to hold to -Werror.
        SYSTEM)
    if(NOT WIN32)
        # Windows finds rmlui.dll beside the executable. Elsewhere a shared
        # RmlUi needs install names and rpaths rewritten for a portable tree;
        # the installed macOS client aborted with "Library not loaded:
        # @rpath/librmlui_debugger.dylib". Link it in instead.
        set(_aos_saved_build_shared_libs "${BUILD_SHARED_LIBS}")
        set(BUILD_SHARED_LIBS OFF)
    endif()
    FetchContent_MakeAvailable(rmlui)
    if(NOT WIN32)
        set(BUILD_SHARED_LIBS "${_aos_saved_build_shared_libs}")
    endif()

    add_library(
        aos_asset_install
        STATIC
            assets/asset_install.cpp
    )
    add_library(BattleSpades::AssetInstall ALIAS aos_asset_install)
    target_include_directories(
        aos_asset_install
        PUBLIC
            "${PROJECT_SOURCE_DIR}/include"
    )
    target_link_libraries(
        aos_asset_install
        PUBLIC
            BattleSpades::BuildOptions
        PRIVATE
            nlohmann_json::nlohmann_json
            unofficial-sodium::sodium
    )
    aos_enable_strict_warnings(aos_asset_install)

    add_executable(
        BattleSpadesAssetInstaller
        assets/asset_installer_main.cpp
    )
    target_link_libraries(
        BattleSpadesAssetInstaller
        PRIVATE
            BattleSpades::AssetInstall
            SDL3::SDL3
    )
    if(WIN32)
        target_sources(
            BattleSpadesAssetInstaller
            PRIVATE
                platform/windows/battlespades_client.rc
                platform/windows/battlespades_client.manifest
                platform/windows/game.ico
        )
        set_source_files_properties(
            platform/windows/battlespades_client.manifest
            platform/windows/game.ico
            PROPERTIES HEADER_FILE_ONLY TRUE
        )
        if(MSVC)
            # The resource script embeds our DPI/long-path-aware manifest.
            # Disable the linker's second generated RT_MANIFEST resource.
            target_link_options(BattleSpadesAssetInstaller PRIVATE /MANIFEST:NO)
        endif()
        if(MSVC AND AOS_WINDOWS_RELEASE_GUI)
            set_property(
                TARGET BattleSpadesAssetInstaller
                PROPERTY WIN32_EXECUTABLE "$<CONFIG:Release>"
            )
            target_compile_definitions(
                BattleSpadesAssetInstaller
                PRIVATE
                    "$<$<CONFIG:Release>:AOS_WINDOWS_GUI_SUBSYSTEM=1>"
            )
        endif()
        add_custom_command(
            TARGET BattleSpadesAssetInstaller
            POST_BUILD
            COMMAND
                "${CMAKE_COMMAND}" -E copy_if_different
                $<TARGET_RUNTIME_DLLS:BattleSpadesAssetInstaller>
                $<TARGET_FILE_DIR:BattleSpadesAssetInstaller>
            COMMAND_EXPAND_LISTS
            VERBATIM
        )
    endif()
    aos_enable_strict_warnings(BattleSpadesAssetInstaller)

    add_library(
        aos_local_host
        STATIC
            platform/local_server_process.cpp
            platform/relay_host_tunnel.cpp
    )
    add_library(BattleSpades::LocalHost ALIAS aos_local_host)
    target_include_directories(aos_local_host PUBLIC "${PROJECT_SOURCE_DIR}/include")
    target_link_libraries(
        aos_local_host
        PUBLIC BattleSpades::BuildOptions
        PRIVATE BattleSpades::Core unofficial-sodium::sodium nlohmann_json::nlohmann_json
    )
    if(WIN32)
        target_link_libraries(aos_local_host PRIVATE ws2_32)
    endif()
    aos_enable_strict_warnings(aos_local_host)

    # Peer-to-peer transport over Valve's relay network. The SDK supplies
    # headers only: libsteam_api is loaded at run time, so a player without
    # Steam still starts the game and falls back to the AoSPlay relay.
    if(AOS_ENABLE_STEAM_NETWORKING)
        FetchContent_Declare(steamworks
            URL https://github.com/rlabrecque/SteamworksSDK/archive/df2baabf574a738ef1ea90a7e89339107fc0a279.tar.gz
            URL_HASH SHA256=4df87731a1179f20a9253a4348f6bd8381a8e46e5e3dc58f9db705ef200eea61
            DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
        FetchContent_MakeAvailable(steamworks)

        add_library(
            aos_steam_net
            STATIC
                platform/steam_networking.cpp
        )
        add_library(BattleSpades::SteamNet ALIAS aos_steam_net)
        target_include_directories(aos_steam_net PUBLIC "${PROJECT_SOURCE_DIR}/include")
        target_include_directories(aos_steam_net SYSTEM PRIVATE "${steamworks_SOURCE_DIR}/public")
        target_link_libraries(aos_steam_net PUBLIC BattleSpades::BuildOptions PRIVATE BattleSpades::Core)
        if(WIN32)
            target_link_libraries(aos_steam_net PRIVATE ws2_32)
        endif()
        aos_enable_strict_warnings(aos_steam_net)

        # Players need the library beside the executable; it is loaded at run
        # time, so a copy that is missing only disables the Steam transport.
        if(WIN32)
            set(_aos_steam_library "${steamworks_SOURCE_DIR}/redistributable_bin/win64/steam_api64.dll")
        elseif(APPLE)
            set(_aos_steam_library "${steamworks_SOURCE_DIR}/redistributable_bin/osx/libsteam_api.dylib")
        else()
            set(_aos_steam_library "${steamworks_SOURCE_DIR}/redistributable_bin/linux64/libsteam_api.so")
        endif()
        install(FILES "${_aos_steam_library}" DESTINATION "${CMAKE_INSTALL_BINDIR}" COMPONENT Runtime)

        add_executable(aos_steam_p2p_smoke "${PROJECT_SOURCE_DIR}/tools/steam_p2p_smoke.cpp")
        target_link_libraries(aos_steam_p2p_smoke PRIVATE BattleSpades::SteamNet)
        aos_enable_strict_warnings(aos_steam_p2p_smoke)
    endif()

    add_library(
        aos_native_steam
        STATIC
            platform/native_steam_client.cpp
            platform/steam_connect.cpp
    )
    add_library(BattleSpades::NativeSteam ALIAS aos_native_steam)
    target_include_directories(aos_native_steam PUBLIC "${PROJECT_SOURCE_DIR}/include")
    target_link_libraries(aos_native_steam PUBLIC BattleSpades::BuildOptions)
    aos_enable_strict_warnings(aos_native_steam)

    add_library(
        aos_network_enet
        STATIC
            network/protocol168_session.cpp
            network/enet_protocol168_client.cpp
            network/live_protocol168_connection.cpp
            network/aosplay_scores.cpp
            network/revival_identity.cpp
            network/revival_social.cpp
            network/server_discovery.cpp
    )
    add_library(BattleSpades::NetworkEnet ALIAS aos_network_enet)
    target_include_directories(
        aos_network_enet
        PUBLIC
            "${PROJECT_SOURCE_DIR}/include"
    )
    target_link_libraries(
        aos_network_enet
        PUBLIC
            BattleSpades::Protocol
            nlohmann_json::nlohmann_json
        PRIVATE
            BattleSpades::Core
            ZLIB::ZLIB
            CURL::libcurl
            unofficial-sodium::sodium
            unofficial::enet::enet
    )
    if(WIN32)
        # The vcpkg ENet static target does not propagate its platform libs.
        target_link_libraries(aos_network_enet PUBLIC ws2_32 winmm crypt32)
    endif()
    aos_enable_strict_warnings(aos_network_enet)

    add_executable(
        aos_protocol168_live_smoke
        "${PROJECT_SOURCE_DIR}/tools/protocol168_live_smoke.cpp"
    )
    target_link_libraries(
        aos_protocol168_live_smoke
        PRIVATE
            BattleSpades::NetworkEnet
    )
    aos_enable_strict_warnings(aos_protocol168_live_smoke)

    add_executable(
        aos_protocol168_live_session_smoke
        "${PROJECT_SOURCE_DIR}/tools/protocol168_live_session_smoke.cpp"
    )
    target_link_libraries(
        aos_protocol168_live_session_smoke
        PRIVATE
            BattleSpades::NetworkEnet
    )
    aos_enable_strict_warnings(aos_protocol168_live_session_smoke)

    add_executable(
        aos_protocol168_deployable_live_smoke
        "${PROJECT_SOURCE_DIR}/tools/protocol168_deployable_live_smoke.cpp"
    )
    target_link_libraries(
        aos_protocol168_deployable_live_smoke
        PRIVATE
            BattleSpades::NetworkEnet
    )
    aos_enable_strict_warnings(aos_protocol168_deployable_live_smoke)

    add_executable(aos_protocol168_local_gameplay_smoke
        "${PROJECT_SOURCE_DIR}/tools/protocol168_local_gameplay_smoke.cpp")
    target_link_libraries(aos_protocol168_local_gameplay_smoke PRIVATE
        BattleSpades::NetworkEnet BattleSpades::Frontend)
    aos_enable_strict_warnings(aos_protocol168_local_gameplay_smoke)

    add_executable(
        aos_protocol168_movement_parity
        "${PROJECT_SOURCE_DIR}/tools/protocol168_movement_parity.cpp"
    )
    target_link_libraries(
        aos_protocol168_movement_parity
        PRIVATE
            BattleSpades::NetworkEnet
    )
    aos_enable_strict_warnings(aos_protocol168_movement_parity)

    add_executable(
        aos_server_discovery_live_smoke
        "${PROJECT_SOURCE_DIR}/tools/server_discovery_live_smoke.cpp"
    )
    target_link_libraries(
        aos_server_discovery_live_smoke
        PRIVATE
            BattleSpades::NetworkEnet
    )
    aos_enable_strict_warnings(aos_server_discovery_live_smoke)

    add_executable(
        aos_aosplay_scores_live_smoke
        "${PROJECT_SOURCE_DIR}/tools/aosplay_scores_live_smoke.cpp"
    )
    target_link_libraries(
        aos_aosplay_scores_live_smoke
        PRIVATE
            BattleSpades::NetworkEnet
    )
    aos_enable_strict_warnings(aos_aosplay_scores_live_smoke)

    add_executable(
        aos_revival_identity_live_smoke
        "${PROJECT_SOURCE_DIR}/tools/revival_identity_live_smoke.cpp"
    )
    target_link_libraries(
        aos_revival_identity_live_smoke
        PRIVATE
            BattleSpades::NetworkEnet
            nlohmann_json::nlohmann_json
    )
    aos_enable_strict_warnings(aos_revival_identity_live_smoke)

    add_executable(
        aos_local_server_host_smoke
        "${PROJECT_SOURCE_DIR}/tools/local_server_host_smoke.cpp"
    )
    target_link_libraries(
        aos_local_server_host_smoke
        PRIVATE
            BattleSpades::LocalHost
            BattleSpades::NativeSteam
            BattleSpades::NetworkEnet
    )
    # --steam-tunnel drives the whole join the way a friend does, through the
    # Steam transport instead of the AoSPlay relay, on one machine.
    if(AOS_ENABLE_STEAM_NETWORKING)
        target_link_libraries(aos_local_server_host_smoke PRIVATE BattleSpades::SteamNet)
        target_compile_definitions(aos_local_server_host_smoke PRIVATE AOS_HAS_STEAM_NETWORKING=1)
    endif()
    aos_enable_strict_warnings(aos_local_server_host_smoke)
    add_executable(aos_cosmetic_replication_smoke "${PROJECT_SOURCE_DIR}/tools/cosmetic_replication_smoke.cpp")
    target_link_libraries(aos_cosmetic_replication_smoke PRIVATE BattleSpades::LocalHost BattleSpades::NativeSteam BattleSpades::NetworkEnet)
    aos_enable_strict_warnings(aos_cosmetic_replication_smoke)

    add_executable(aos_social_lobby_live_smoke "${PROJECT_SOURCE_DIR}/tools/social_lobby_live_smoke.cpp")
    target_link_libraries(aos_social_lobby_live_smoke PRIVATE
        BattleSpades::LocalHost BattleSpades::NativeSteam BattleSpades::NetworkEnet)
    aos_enable_strict_warnings(aos_social_lobby_live_smoke)

    add_executable(
        aos_ugc_local_server_host_smoke
        "${PROJECT_SOURCE_DIR}/tools/ugc_local_server_host_smoke.cpp"
    )
    target_link_libraries(
        aos_ugc_local_server_host_smoke
        PRIVATE
            BattleSpades::LocalHost
            BattleSpades::NetworkEnet
    )
    aos_enable_strict_warnings(aos_ugc_local_server_host_smoke)

    add_library(
        aos_platform_sdl
        STATIC
            platform/sdl_window_module.cpp
    )
    add_library(BattleSpades::PlatformSdl ALIAS aos_platform_sdl)
    target_include_directories(
        aos_platform_sdl
        PUBLIC
            "${PROJECT_SOURCE_DIR}/include"
    )
    target_link_libraries(
        aos_platform_sdl
        PUBLIC
            BattleSpades::Ports
        PRIVATE
            SDL3::SDL3
    )
    aos_enable_strict_warnings(aos_platform_sdl)

    add_library(
        aos_render_bgfx
        STATIC
            render/bgfx_ui_renderer.cpp
            render/texture_quality.cpp
            render/world_renderer.cpp
    )
    add_library(BattleSpades::RenderBgfx ALIAS aos_render_bgfx)
    target_include_directories(
        aos_render_bgfx
        PUBLIC
            "${PROJECT_SOURCE_DIR}/include"
    )
    target_link_libraries(
        aos_render_bgfx
        PUBLIC
            BattleSpades::BuildOptions
            BattleSpades::World
        PRIVATE
            BattleSpades::Ui
            bgfx::bgfx
            bgfx::bimg_decode
            nlohmann_json::nlohmann_json
    )
    aos_enable_strict_warnings(aos_render_bgfx)

    add_executable(
        aos_graphics_backend_probe
        "${PROJECT_SOURCE_DIR}/tools/graphics_backend_probe.cpp"
    )
    target_link_libraries(
        aos_graphics_backend_probe
        PRIVATE
            BattleSpades::RenderBgfx
    )
    aos_enable_strict_warnings(aos_graphics_backend_probe)

    add_library(
        aos_text
        STATIC
            text/text_rasterizer.cpp
    )
    add_library(BattleSpades::Text ALIAS aos_text)
    target_include_directories(
        aos_text
        PUBLIC
            "${PROJECT_SOURCE_DIR}/include"
    )
    target_link_libraries(
        aos_text
        PUBLIC
            BattleSpades::BuildOptions
        PRIVATE
            Freetype::Freetype
            harfbuzz::harfbuzz
    )
    aos_enable_strict_warnings(aos_text)

    add_library(
        aos_audio_openal
        STATIC
            audio/openal_frontend_audio.cpp
    )
    add_library(BattleSpades::AudioOpenAl ALIAS aos_audio_openal)
    target_include_directories(
        aos_audio_openal
        PUBLIC
            "${PROJECT_SOURCE_DIR}/include"
    )
    target_include_directories(aos_audio_openal SYSTEM PRIVATE "${Stb_INCLUDE_DIR}")
    target_link_libraries(
        aos_audio_openal
        PUBLIC
            BattleSpades::Ports
        PRIVATE
            BattleSpades::World
            OpenAL::OpenAL
    )
    aos_enable_strict_warnings(aos_audio_openal)
    if(APPLE)
        # Apple's supported compatibility framework is intentionally used for
        # this first native port, but all OpenAL declarations are annotated as
        # deprecated in current SDKs. Keep every other strict diagnostic on.
        target_compile_options(aos_audio_openal PRIVATE -Wno-deprecated-declarations)
    endif()

    add_library(
        aos_frontend_native
        STATIC
            frontend/native_texture_preloader.cpp
            frontend/ugc_preview_writer.cpp
            frontend/inventory_session.cpp
            frontend/scripted_weapon_images.cpp
            frontend/inventory_view.cpp
            frontend/cosmetic_icons.cpp
            frontend/native_frontend_module.cpp
    )
    add_library(BattleSpades::FrontendNative ALIAS aos_frontend_native)
    target_include_directories(
        aos_frontend_native
        PUBLIC
            "${PROJECT_SOURCE_DIR}/include"
    )
    target_include_directories(aos_frontend_native SYSTEM PRIVATE "${Stb_INCLUDE_DIR}")
    set_source_files_properties(
        frontend/ugc_preview_writer.cpp
        PROPERTIES COMPILE_DEFINITIONS "_CRT_SECURE_NO_WARNINGS"
    )
    if(AOS_ENABLE_STEAM_NETWORKING)
        target_link_libraries(aos_frontend_native PRIVATE BattleSpades::SteamNet)
        target_compile_definitions(aos_frontend_native PRIVATE AOS_HAS_STEAM_NETWORKING=1)
    endif()
    target_compile_definitions(aos_frontend_native PRIVATE
        AOS_DEV_TRAINING_VXL="${PROJECT_SOURCE_DIR}/../BattleSpades/maps/Training.vxl"
        AOS_DEV_BATTLESPADES_ROOT="${PROJECT_SOURCE_DIR}/../BattleSpades")
    target_link_libraries(
        aos_frontend_native
        PUBLIC
            BattleSpades::Frontend
            BattleSpades::Ports
        PRIVATE
            BattleSpades::RenderBgfx
            BattleSpades::Text
            BattleSpades::AudioOpenAl
            BattleSpades::LocalHost
            BattleSpades::NetworkEnet
            BattleSpades::World
            unofficial-sodium::sodium
            RmlUi::RmlUi
    )
    aos_enable_strict_warnings(aos_frontend_native)
    if(MSVC)
        # native_frontend_module is intentionally a composition boundary and
        # Debug codegen can exceed COFF's default section count.
        target_compile_options(aos_frontend_native PRIVATE /bigobj)
    endif()

    # Shader binaries are checked in because shaderc's compiler toolchain need not
    # match the application ABI. Keeping them beside their source also makes a
    # clean client build independent of bgfx's optional tools feature.
    set(AOS_SHADER_OUTPUT_ROOT "${PROJECT_SOURCE_DIR}/src/render/shaders/bin")
    foreach(backend IN ITEMS dx11 glsl essl spirv metal)
        foreach(shader IN ITEMS vs_ui fs_ui vs_world fs_world vs_skydome fs_skydome
                                vs_particle fs_particle vs_shadow fs_shadow)
            set(shader_binary "${AOS_SHADER_OUTPUT_ROOT}/${backend}/${shader}.bin")
            if(NOT EXISTS "${shader_binary}")
                message(FATAL_ERROR "Required UI shader is missing: ${shader_binary}")
            endif()
        endforeach()
    endforeach()
endif()

add_executable(BattleSpadesClient main.cpp)
if(WIN32)
    target_sources(
        BattleSpadesClient
        PRIVATE
            platform/windows/battlespades_client.rc
            platform/windows/battlespades_client.manifest
            platform/windows/game.ico
    )
    set_source_files_properties(
        platform/windows/battlespades_client.manifest
        platform/windows/game.ico
        PROPERTIES HEADER_FILE_ONLY TRUE
    )

    if(MSVC)
        target_link_options(BattleSpadesClient PRIVATE /MANIFEST:NO)
    endif()

    if(MSVC AND AOS_ENABLE_NATIVE_BACKENDS AND AOS_WINDOWS_RELEASE_GUI)
        # Only the shipped native Release configuration is a GUI-subsystem
        # application. Debug/Dev and dependency-free headless builds retain a
        # normal console entry point for diagnostics and automated tests.
        set_property(
            TARGET BattleSpadesClient
            PROPERTY WIN32_EXECUTABLE "$<CONFIG:Release>"
        )
        target_compile_definitions(
            BattleSpadesClient
            PRIVATE
                "$<$<CONFIG:Release>:AOS_WINDOWS_GUI_SUBSYSTEM=1>"
        )
    endif()
endif()
target_link_libraries(
    BattleSpadesClient
    PRIVATE
        BattleSpades::Headless
        BattleSpades::Ports
)
if(AOS_ENABLE_NATIVE_BACKENDS)
    target_link_libraries(
        BattleSpadesClient
        PRIVATE
            BattleSpades::PlatformSdl
            BattleSpades::RenderBgfx
            BattleSpades::Text
            BattleSpades::AudioOpenAl
            BattleSpades::FrontendNative
            BattleSpades::AssetInstall
            BattleSpades::NativeSteam
    )
    if(APPLE)
        # main.cpp owns the macOS startup-error dialog, so it consumes the SDL
        # public API directly rather than relying on a private transitive include.
        target_link_libraries(BattleSpadesClient PRIVATE SDL3::SDL3)
    endif()
    if(AOS_BUILD_PROFILE STREQUAL "release")
        set(_aos_developer_asset_root "")
        set(_aos_developer_asset_manifest "")
        set(_aos_developer_shader_root "")
    else()
        set(_aos_developer_asset_root "${AOS_ASSET_ROOT}")
        set(
            _aos_developer_asset_manifest
            "${PROJECT_SOURCE_DIR}/assets/catalog/original-assets.json"
        )
        set(_aos_developer_shader_root "${AOS_SHADER_OUTPUT_ROOT}")
    endif()
    target_compile_definitions(
        BattleSpadesClient
        PRIVATE
            AOS_HAS_NATIVE_BACKENDS=1
            AOS_DEVELOPER_ASSET_ROOT="${_aos_developer_asset_root}"
            AOS_DEVELOPER_ASSET_MANIFEST="${_aos_developer_asset_manifest}"
            AOS_DEVELOPER_SHADER_ROOT="${_aos_developer_shader_root}"
    )
    add_custom_command(
        TARGET BattleSpadesClient
        POST_BUILD
        COMMAND
            "${CMAKE_COMMAND}" -E copy_if_different
            "${PROJECT_SOURCE_DIR}/assets/catalog/original-assets.json"
            "$<TARGET_FILE_DIR:BattleSpadesClient>/asset-manifest.json"
        COMMAND
            "${CMAKE_COMMAND}" -E copy_directory
            "${AOS_SHADER_OUTPUT_ROOT}"
            "$<TARGET_FILE_DIR:BattleSpadesClient>/shaders"
        # Developer builds run directly from the target directory. Keep the
        # project-owned UI overlay beside the imported retail tree just as the
        # install/package rules do; otherwise mandatory additions such as the
        # Friends button fail the bounded preload and close the client.
        COMMAND
            "${CMAKE_COMMAND}" -E copy_directory
            "${PROJECT_SOURCE_DIR}/assets/client"
            "$<TARGET_FILE_DIR:BattleSpadesClient>/assets/client"
        # The executable is also a supported developer launch point. Keep the
        # external UTF-8 packs next to it so launching from out/build behaves
        # exactly like a staged or installed client.
        COMMAND
            "${CMAKE_COMMAND}" -E copy_directory
            "${PROJECT_SOURCE_DIR}/config/localization"
            "$<TARGET_FILE_DIR:BattleSpadesClient>/localization"
        COMMAND
            "${CMAKE_COMMAND}" -E copy_if_different
            "${PROJECT_SOURCE_DIR}/config/ui-layout.json"
            "$<TARGET_FILE_DIR:BattleSpadesClient>/ui-layout.json"
        VERBATIM
    )
    # The Steamworks library is loaded by name from the executable's directory.
    # The install rules place it there for players; developer runs and the
    # staging script take it from the target directory, so copy it as well.
    if(AOS_ENABLE_STEAM_NETWORKING AND _aos_steam_library)
        add_custom_command(
            TARGET BattleSpadesClient
            POST_BUILD
            COMMAND
                "${CMAKE_COMMAND}" -E copy_if_different
                "${_aos_steam_library}"
                "$<TARGET_FILE_DIR:BattleSpadesClient>"
            VERBATIM
        )
    endif()
    if(WIN32)
        set(
            AOS_STEAM_BRIDGE_EXECUTABLE
            "${PROJECT_SOURCE_DIR}/out/steam-bridge/BattleSpadesSteamBridge32.exe"
            CACHE FILEPATH "Prebuilt x86 helper for the retail Steamworks runtime"
        )
        if(NOT EXISTS "${AOS_STEAM_BRIDGE_EXECUTABLE}")
            message(FATAL_ERROR
                "The x86 Steam bridge is missing. Run scripts/build-steam-bridge.ps1 first.")
        endif()

        # TARGET_RUNTIME_DLLS is empty on non-DLL platforms. Keeping this
        # command Windows-only avoids issuing copy_if_different with no source
        # arguments on Linux and macOS native builds.
        add_custom_command(
            TARGET BattleSpadesClient
            POST_BUILD
            COMMAND
                "${CMAKE_COMMAND}" -E copy_if_different
                $<TARGET_RUNTIME_DLLS:BattleSpadesClient>
                $<TARGET_FILE_DIR:BattleSpadesClient>
            COMMAND_EXPAND_LISTS
            VERBATIM
        )

        # The retail Steam install launches `aos.exe`.  Keep the descriptive
        # executable as the canonical build artifact, but materialize an exact
        # byte-for-byte alias beside it so testers can overlay the portable
        # client onto a Steam installation and retain Steam play-time tracking.
        # This is intentionally a copy, not a second target: both entry points
        # must execute the same tested binary and resolve the same adjacent
        # DLLs/assets.
        add_custom_command(
            TARGET BattleSpadesClient
            POST_BUILD
            COMMAND
                "${CMAKE_COMMAND}" -E copy_if_different
                "${AOS_STEAM_BRIDGE_EXECUTABLE}"
                "$<TARGET_FILE_DIR:BattleSpadesClient>/BattleSpadesSteamBridge32.exe"
            COMMAND
                "${CMAKE_COMMAND}" -E copy_if_different
                "$<TARGET_FILE:BattleSpadesClient>"
                "$<TARGET_FILE_DIR:BattleSpadesClient>/aos.exe"
            VERBATIM
        )
    endif()
endif()
aos_enable_strict_warnings(BattleSpadesClient)

set_target_properties(
    BattleSpadesClient
    PROPERTIES
        OUTPUT_NAME "BattleSpadesClient"
        VS_DEBUGGER_WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
)
