add_executable(
    aos_core_tests
    test_main.cpp
)

target_link_libraries(
    aos_core_tests
    PRIVATE
        BattleSpades::Headless
        BattleSpades::Ports
)
aos_enable_strict_warnings(aos_core_tests)

add_test(NAME aos_core_tests COMMAND aos_core_tests)

add_executable(aos_hud_layout_tests test_hud_layout.cpp)
target_link_libraries(aos_hud_layout_tests PRIVATE BattleSpades::Frontend)
aos_enable_strict_warnings(aos_hud_layout_tests)
add_test(NAME aos_hud_layout_tests COMMAND aos_hud_layout_tests)

add_executable(aos_game_hud_tests test_game_hud.cpp)
target_link_libraries(aos_game_hud_tests PRIVATE BattleSpades::Frontend BattleSpades::World)
aos_enable_strict_warnings(aos_game_hud_tests)
add_test(NAME aos_game_hud_tests COMMAND aos_game_hud_tests)

add_executable(aos_match_overlays_tests test_match_overlays.cpp)
target_link_libraries(aos_match_overlays_tests PRIVATE BattleSpades::Frontend)
aos_enable_strict_warnings(aos_match_overlays_tests)
add_test(NAME aos_match_overlays_tests COMMAND aos_match_overlays_tests)

add_executable(aos_player_progression_tests test_player_progression.cpp)
target_link_libraries(aos_player_progression_tests PRIVATE BattleSpades::Frontend)
aos_enable_strict_warnings(aos_player_progression_tests)
add_test(NAME aos_player_progression_tests COMMAND aos_player_progression_tests)

add_executable(aos_pause_menu_tests test_pause_menu.cpp)
target_link_libraries(aos_pause_menu_tests PRIVATE BattleSpades::Frontend)
aos_enable_strict_warnings(aos_pause_menu_tests)
add_test(NAME aos_pause_menu_tests COMMAND aos_pause_menu_tests)

add_executable(aos_camera_basis_tests test_camera_basis.cpp)
target_link_libraries(aos_camera_basis_tests PRIVATE BattleSpades::World)
aos_enable_strict_warnings(aos_camera_basis_tests)
add_test(NAME aos_camera_basis_tests COMMAND aos_camera_basis_tests)

add_executable(aos_death_camera_tests test_death_camera.cpp)
target_link_libraries(aos_death_camera_tests PRIVATE BattleSpades::Frontend)
aos_enable_strict_warnings(aos_death_camera_tests)
add_test(NAME aos_death_camera_tests COMMAND aos_death_camera_tests)

add_executable(aos_jetpack_death_tests test_jetpack_death.cpp)
target_link_libraries(aos_jetpack_death_tests PRIVATE BattleSpades::World)
aos_enable_strict_warnings(aos_jetpack_death_tests)
add_test(NAME aos_jetpack_death_tests COMMAND aos_jetpack_death_tests)

add_executable(aos_tutorial_session_tests test_tutorial_session.cpp)
target_link_libraries(aos_tutorial_session_tests PRIVATE BattleSpades::World)
target_compile_definitions(aos_tutorial_session_tests PRIVATE
    AOS_TRAINING_VXL="${PROJECT_SOURCE_DIR}/../BattleSpades/maps/Training.vxl")
aos_enable_strict_warnings(aos_tutorial_session_tests)
add_test(NAME aos_tutorial_session_tests COMMAND aos_tutorial_session_tests)

add_executable(aos_retail_view_model_tests test_retail_view_model.cpp)
target_link_libraries(aos_retail_view_model_tests PRIVATE BattleSpades::World)
aos_enable_strict_warnings(aos_retail_view_model_tests)
add_test(NAME aos_retail_view_model_tests COMMAND aos_retail_view_model_tests)

add_executable(aos_retail_character_pose_tests test_retail_character_pose.cpp)
target_link_libraries(aos_retail_character_pose_tests PRIVATE BattleSpades::World)
aos_enable_strict_warnings(aos_retail_character_pose_tests)
add_test(NAME aos_retail_character_pose_tests COMMAND aos_retail_character_pose_tests)

add_executable(aos_player_name_projection_tests test_player_name_projection.cpp)
target_link_libraries(aos_player_name_projection_tests PRIVATE BattleSpades::Frontend)
aos_enable_strict_warnings(aos_player_name_projection_tests)
add_test(NAME aos_player_name_projection_tests COMMAND aos_player_name_projection_tests)

add_executable(aos_objective_indicator_tests test_objective_indicator.cpp)
target_link_libraries(aos_objective_indicator_tests PRIVATE BattleSpades::Frontend)
aos_enable_strict_warnings(aos_objective_indicator_tests)
add_test(NAME aos_objective_indicator_tests COMMAND aos_objective_indicator_tests)

add_executable(aos_zombie_conversion_audio_tests test_zombie_conversion_audio.cpp)
target_link_libraries(aos_zombie_conversion_audio_tests PRIVATE BattleSpades::Frontend)
aos_enable_strict_warnings(aos_zombie_conversion_audio_tests)
add_test(NAME aos_zombie_conversion_audio_tests COMMAND aos_zombie_conversion_audio_tests)

add_executable(aos_retail_inventory_tests test_retail_inventory.cpp)
target_link_libraries(aos_retail_inventory_tests PRIVATE BattleSpades::World)
aos_enable_strict_warnings(aos_retail_inventory_tests)
add_test(NAME aos_retail_inventory_tests COMMAND aos_retail_inventory_tests)

add_executable(aos_player_movement_tests test_player_movement.cpp)
target_link_libraries(aos_player_movement_tests PRIVATE BattleSpades::World)
aos_enable_strict_warnings(aos_player_movement_tests)
add_test(NAME aos_player_movement_tests COMMAND aos_player_movement_tests)

add_executable(aos_chunk_mesher_tests test_chunk_mesher.cpp)
target_link_libraries(aos_chunk_mesher_tests PRIVATE BattleSpades::World)
aos_enable_strict_warnings(aos_chunk_mesher_tests)
add_test(NAME aos_chunk_mesher_tests COMMAND aos_chunk_mesher_tests)

add_executable(aos_voxel_collapse_tests test_voxel_collapse.cpp)
target_link_libraries(aos_voxel_collapse_tests PRIVATE BattleSpades::World)
aos_enable_strict_warnings(aos_voxel_collapse_tests)
add_test(NAME aos_voxel_collapse_tests COMMAND aos_voxel_collapse_tests)

add_executable(aos_terrain_effects_tests test_terrain_effects.cpp)
target_link_libraries(aos_terrain_effects_tests PRIVATE BattleSpades::World)
aos_enable_strict_warnings(aos_terrain_effects_tests)
add_test(NAME aos_terrain_effects_tests COMMAND aos_terrain_effects_tests)

add_executable(aos_voxel_raycast_tests test_voxel_raycast.cpp)
target_link_libraries(aos_voxel_raycast_tests PRIVATE BattleSpades::World)
aos_enable_strict_warnings(aos_voxel_raycast_tests)
add_test(NAME aos_voxel_raycast_tests COMMAND aos_voxel_raycast_tests)

add_executable(aos_sniper_laser_tests test_sniper_laser.cpp)
target_link_libraries(aos_sniper_laser_tests PRIVATE BattleSpades::World)
aos_enable_strict_warnings(aos_sniper_laser_tests)
add_test(NAME aos_sniper_laser_tests COMMAND aos_sniper_laser_tests)

add_executable(aos_replicated_shot_tests test_replicated_shot.cpp)
target_link_libraries(aos_replicated_shot_tests PRIVATE BattleSpades::World)
aos_enable_strict_warnings(aos_replicated_shot_tests)
add_test(NAME aos_replicated_shot_tests COMMAND aos_replicated_shot_tests)

add_executable(aos_map_catalog_tests test_map_catalog.cpp)
target_link_libraries(aos_map_catalog_tests PRIVATE BattleSpades::World)
target_compile_definitions(aos_map_catalog_tests PRIVATE
    AOS_TEST_ASSET_ROOT="${AOS_ASSET_ROOT}")
aos_enable_strict_warnings(aos_map_catalog_tests)
add_test(NAME aos_map_catalog_tests COMMAND aos_map_catalog_tests)

add_executable(aos_emissive_set_tests test_emissive_set.cpp)
target_link_libraries(aos_emissive_set_tests PRIVATE BattleSpades::World)
aos_enable_strict_warnings(aos_emissive_set_tests)
add_test(NAME aos_emissive_set_tests COMMAND aos_emissive_set_tests)

add_executable(aos_kv6_normal_tests test_kv6_normals.cpp)
target_link_libraries(aos_kv6_normal_tests PRIVATE BattleSpades::World)
target_compile_definitions(aos_kv6_normal_tests PRIVATE
    AOS_TEST_ASSET_ROOT="${AOS_ASSET_ROOT}")
aos_enable_strict_warnings(aos_kv6_normal_tests)
add_test(NAME aos_kv6_normal_tests COMMAND aos_kv6_normal_tests)

add_executable(aos_model_quality_tests test_model_quality.cpp)
target_link_libraries(aos_model_quality_tests PRIVATE BattleSpades::World)
target_compile_definitions(aos_model_quality_tests PRIVATE
    AOS_TEST_ASSET_ROOT="${AOS_ASSET_ROOT}")
aos_enable_strict_warnings(aos_model_quality_tests)
add_test(NAME aos_model_quality_tests COMMAND aos_model_quality_tests)

add_executable(aos_prefab_placement_tests test_prefab_placement.cpp)
target_link_libraries(aos_prefab_placement_tests PRIVATE BattleSpades::World)
aos_enable_strict_warnings(aos_prefab_placement_tests)
add_test(NAME aos_prefab_placement_tests COMMAND aos_prefab_placement_tests)

add_executable(aos_ugc_prefab_control_tests test_ugc_prefab_control.cpp)
target_link_libraries(aos_ugc_prefab_control_tests PRIVATE BattleSpades::World)
aos_enable_strict_warnings(aos_ugc_prefab_control_tests)
add_test(NAME aos_ugc_prefab_control_tests COMMAND aos_ugc_prefab_control_tests)

add_executable(aos_quality_profile_tests test_quality_profile.cpp)
target_link_libraries(aos_quality_profile_tests PRIVATE BattleSpades::Settings)
aos_enable_strict_warnings(aos_quality_profile_tests)
add_test(NAME aos_quality_profile_tests COMMAND aos_quality_profile_tests)

add_executable(aos_map_atmosphere_tests test_map_atmosphere.cpp)
target_link_libraries(aos_map_atmosphere_tests PRIVATE BattleSpades::World)
target_compile_definitions(aos_map_atmosphere_tests PRIVATE
    AOS_TEST_ASSET_ROOT="${AOS_ASSET_ROOT}")
aos_enable_strict_warnings(aos_map_atmosphere_tests)
add_test(NAME aos_map_atmosphere_tests COMMAND aos_map_atmosphere_tests)

add_executable(aos_particle_system_tests test_particle_system.cpp)
target_link_libraries(aos_particle_system_tests PRIVATE BattleSpades::World)
target_compile_definitions(aos_particle_system_tests PRIVATE
    AOS_TEST_ASSET_ROOT="${AOS_ASSET_ROOT}")
aos_enable_strict_warnings(aos_particle_system_tests)
add_test(NAME aos_particle_system_tests COMMAND aos_particle_system_tests)

add_executable(aos_runtime_vfx_adapter_tests test_runtime_vfx_adapter.cpp)
target_link_libraries(aos_runtime_vfx_adapter_tests PRIVATE BattleSpades::Frontend)
aos_enable_strict_warnings(aos_runtime_vfx_adapter_tests)
add_test(NAME aos_runtime_vfx_adapter_tests COMMAND aos_runtime_vfx_adapter_tests)

add_executable(aos_protocol168_terrain_tests test_protocol168_terrain.cpp)
target_link_libraries(aos_protocol168_terrain_tests PRIVATE BattleSpades::Protocol)
aos_enable_strict_warnings(aos_protocol168_terrain_tests)
add_test(NAME aos_protocol168_terrain_tests COMMAND aos_protocol168_terrain_tests)

add_executable(aos_protocol168_weapons_tests test_protocol168_weapons.cpp)
target_link_libraries(aos_protocol168_weapons_tests PRIVATE BattleSpades::Protocol)
aos_enable_strict_warnings(aos_protocol168_weapons_tests)
add_test(NAME aos_protocol168_weapons_tests COMMAND aos_protocol168_weapons_tests)

add_executable(aos_protocol168_tool_actions_tests test_protocol168_tool_actions.cpp)
target_link_libraries(aos_protocol168_tool_actions_tests PRIVATE BattleSpades::Protocol)
aos_enable_strict_warnings(aos_protocol168_tool_actions_tests)
add_test(NAME aos_protocol168_tool_actions_tests COMMAND aos_protocol168_tool_actions_tests)

add_executable(aos_protocol168_ugc_tests test_protocol168_ugc.cpp)
target_link_libraries(aos_protocol168_ugc_tests PRIVATE BattleSpades::Protocol)
aos_enable_strict_warnings(aos_protocol168_ugc_tests)
add_test(NAME aos_protocol168_ugc_tests COMMAND aos_protocol168_ugc_tests)

add_executable(aos_ugc_project_repository_tests test_ugc_project_repository.cpp)
target_link_libraries(aos_ugc_project_repository_tests PRIVATE BattleSpades::Frontend)
aos_enable_strict_warnings(aos_ugc_project_repository_tests)
add_test(NAME aos_ugc_project_repository_tests COMMAND aos_ugc_project_repository_tests)

add_executable(aos_protocol168_weapon_action_adapter_tests
               test_protocol168_weapon_action_adapter.cpp)
target_link_libraries(aos_protocol168_weapon_action_adapter_tests
                      PRIVATE BattleSpades::Protocol)
aos_enable_strict_warnings(aos_protocol168_weapon_action_adapter_tests)
add_test(NAME aos_protocol168_weapon_action_adapter_tests
         COMMAND aos_protocol168_weapon_action_adapter_tests)

add_executable(aos_protocol168_players_tests test_protocol168_players.cpp)
target_link_libraries(aos_protocol168_players_tests PRIVATE BattleSpades::Protocol)
aos_enable_strict_warnings(aos_protocol168_players_tests)
add_test(NAME aos_protocol168_players_tests COMMAND aos_protocol168_players_tests)

add_executable(aos_protocol168_runtime_tests test_protocol168_runtime.cpp)
target_link_libraries(aos_protocol168_runtime_tests PRIVATE BattleSpades::Protocol)
aos_enable_strict_warnings(aos_protocol168_runtime_tests)
add_test(NAME aos_protocol168_runtime_tests COMMAND aos_protocol168_runtime_tests)

add_executable(aos_weapon_catalog_tests test_weapon_catalog.cpp)
target_link_libraries(aos_weapon_catalog_tests PRIVATE BattleSpades::World)
target_compile_definitions(aos_weapon_catalog_tests PRIVATE
    AOS_TEST_ASSET_ROOT="${AOS_ASSET_ROOT}")
aos_enable_strict_warnings(aos_weapon_catalog_tests)
add_test(NAME aos_weapon_catalog_tests COMMAND aos_weapon_catalog_tests)

add_executable(aos_weapon_audio_map_tests test_weapon_audio_map.cpp)
target_link_libraries(aos_weapon_audio_map_tests PRIVATE BattleSpades::World)
target_compile_definitions(aos_weapon_audio_map_tests PRIVATE
    AOS_TEST_ASSET_ROOT="${AOS_ASSET_ROOT}")
aos_enable_strict_warnings(aos_weapon_audio_map_tests)
add_test(NAME aos_weapon_audio_map_tests COMMAND aos_weapon_audio_map_tests)

add_executable(aos_server_audio_catalog_tests test_server_audio_catalog.cpp)
target_link_libraries(aos_server_audio_catalog_tests PRIVATE BattleSpades::Ports)
target_compile_definitions(aos_server_audio_catalog_tests PRIVATE
    AOS_TEST_ASSET_ROOT="${AOS_ASSET_ROOT}")
aos_enable_strict_warnings(aos_server_audio_catalog_tests)
add_test(NAME aos_server_audio_catalog_tests COMMAND aos_server_audio_catalog_tests)

add_executable(aos_weapon_models_tests test_weapon_models.cpp)
target_link_libraries(aos_weapon_models_tests PRIVATE BattleSpades::World)
target_compile_definitions(aos_weapon_models_tests PRIVATE
    AOS_TEST_ASSET_ROOT="${AOS_ASSET_ROOT}")
aos_enable_strict_warnings(aos_weapon_models_tests)
add_test(NAME aos_weapon_models_tests COMMAND aos_weapon_models_tests)

add_executable(aos_disguise_models_tests test_disguise_models.cpp)
target_link_libraries(aos_disguise_models_tests PRIVATE BattleSpades::World)
target_compile_definitions(aos_disguise_models_tests PRIVATE
    AOS_TEST_ASSET_ROOT="${AOS_ASSET_ROOT}")
aos_enable_strict_warnings(aos_disguise_models_tests)
add_test(NAME aos_disguise_models_tests COMMAND aos_disguise_models_tests)

add_executable(aos_ads_sight_pin_tests test_ads_sight_pin.cpp)
target_link_libraries(aos_ads_sight_pin_tests PRIVATE BattleSpades::World)
target_compile_definitions(aos_ads_sight_pin_tests PRIVATE
    AOS_TEST_ASSET_ROOT="${AOS_ASSET_ROOT}")
aos_enable_strict_warnings(aos_ads_sight_pin_tests)
add_test(NAME aos_ads_sight_pin_tests COMMAND aos_ads_sight_pin_tests)

add_executable(aos_weapon_state_tests test_weapon_state.cpp)
target_link_libraries(aos_weapon_state_tests PRIVATE BattleSpades::World)
aos_enable_strict_warnings(aos_weapon_state_tests)
add_test(NAME aos_weapon_state_tests COMMAND aos_weapon_state_tests)

add_executable(aos_weapon_runtime_tests test_weapon_runtime.cpp)
target_link_libraries(aos_weapon_runtime_tests PRIVATE BattleSpades::World)
aos_enable_strict_warnings(aos_weapon_runtime_tests)
add_test(NAME aos_weapon_runtime_tests COMMAND aos_weapon_runtime_tests)

add_executable(aos_class_voice_tests test_class_voice.cpp)
target_link_libraries(aos_class_voice_tests PRIVATE BattleSpades::World)
target_compile_definitions(aos_class_voice_tests PRIVATE
    AOS_TEST_ASSET_ROOT="${AOS_ASSET_ROOT}")
aos_enable_strict_warnings(aos_class_voice_tests)
add_test(NAME aos_class_voice_tests COMMAND aos_class_voice_tests)

add_executable(aos_footstep_audio_tests test_footstep_audio.cpp)
target_link_libraries(aos_footstep_audio_tests PRIVATE BattleSpades::World)
target_compile_definitions(aos_footstep_audio_tests PRIVATE
    AOS_TEST_ASSET_ROOT="${AOS_ASSET_ROOT}")
aos_enable_strict_warnings(aos_footstep_audio_tests)
add_test(NAME aos_footstep_audio_tests COMMAND aos_footstep_audio_tests)

add_executable(aos_remote_character_audio_tests test_remote_character_audio.cpp)
target_link_libraries(aos_remote_character_audio_tests PRIVATE BattleSpades::World)
aos_enable_strict_warnings(aos_remote_character_audio_tests)
add_test(NAME aos_remote_character_audio_tests COMMAND aos_remote_character_audio_tests)

add_executable(aos_entity_catalog_tests test_entity_catalog.cpp)
target_link_libraries(aos_entity_catalog_tests PRIVATE BattleSpades::World)
target_compile_definitions(aos_entity_catalog_tests PRIVATE
    AOS_TEST_ASSET_ROOT="${AOS_ASSET_ROOT}")
aos_enable_strict_warnings(aos_entity_catalog_tests)
add_test(NAME aos_entity_catalog_tests COMMAND aos_entity_catalog_tests)

add_executable(aos_local_entity_tests test_local_entity.cpp)
target_link_libraries(aos_local_entity_tests PRIVATE BattleSpades::World)
aos_enable_strict_warnings(aos_local_entity_tests)
add_test(NAME aos_local_entity_tests COMMAND aos_local_entity_tests)

add_executable(aos_weapon_fire_sound_tests test_weapon_fire_sound.cpp)
target_link_libraries(aos_weapon_fire_sound_tests PRIVATE BattleSpades::World)
aos_enable_strict_warnings(aos_weapon_fire_sound_tests)
add_test(NAME aos_weapon_fire_sound_tests COMMAND aos_weapon_fire_sound_tests)

add_executable(aos_weapon_zoom_tests test_weapon_zoom.cpp)
target_link_libraries(aos_weapon_zoom_tests PRIVATE BattleSpades::World)
aos_enable_strict_warnings(aos_weapon_zoom_tests)
add_test(NAME aos_weapon_zoom_tests COMMAND aos_weapon_zoom_tests)

add_executable(aos_class_catalog_tests test_class_catalog.cpp)
target_link_libraries(aos_class_catalog_tests PRIVATE BattleSpades::World)
target_compile_definitions(aos_class_catalog_tests PRIVATE
    AOS_TEST_ASSET_ROOT="${AOS_ASSET_ROOT}")
aos_enable_strict_warnings(aos_class_catalog_tests)
add_test(NAME aos_class_catalog_tests COMMAND aos_class_catalog_tests)

add_executable(aos_player_inventory_tests test_player_inventory.cpp)
target_link_libraries(aos_player_inventory_tests PRIVATE BattleSpades::World)
aos_enable_strict_warnings(aos_player_inventory_tests)
add_test(NAME aos_player_inventory_tests COMMAND aos_player_inventory_tests)

add_executable(aos_class_selection_menu_tests test_class_selection_menu.cpp)
target_link_libraries(aos_class_selection_menu_tests PRIVATE BattleSpades::Frontend)
aos_enable_strict_warnings(aos_class_selection_menu_tests)
add_test(NAME aos_class_selection_menu_tests COMMAND aos_class_selection_menu_tests)

add_executable(aos_ugc_loadout_menu_tests test_ugc_loadout_menu.cpp)
target_link_libraries(aos_ugc_loadout_menu_tests PRIVATE BattleSpades::Frontend)
aos_enable_strict_warnings(aos_ugc_loadout_menu_tests)
add_test(NAME aos_ugc_loadout_menu_tests COMMAND aos_ugc_loadout_menu_tests)

add_executable(aos_gameplay_debug_lab_tests test_gameplay_debug_lab.cpp)
target_link_libraries(aos_gameplay_debug_lab_tests PRIVATE
    BattleSpades::Frontend BattleSpades::World)
aos_enable_strict_warnings(aos_gameplay_debug_lab_tests)
add_test(NAME aos_gameplay_debug_lab_tests COMMAND aos_gameplay_debug_lab_tests)

# In the combined revival workspace, fail the suite as soon as the checked-in
# native table drifts from the authoritative Python server. Standalone source
# archives remain buildable without Python or the sibling server checkout.
find_package(Python3 COMPONENTS Interpreter QUIET)
set(AOS_SIBLING_SERVER_ROOT "${PROJECT_SOURCE_DIR}/../BattleSpades")
set(AOS_RETAIL_SOURCE_ROOT "${PROJECT_SOURCE_DIR}/../aceofspades_source")
if(Python3_Interpreter_FOUND)
    add_test(
        NAME aos_asset_catalog_policy_tests
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/test_assets_tool.py"
    )
endif()
if(Python3_Interpreter_FOUND AND
   EXISTS "${AOS_SIBLING_SERVER_ROOT}/server/game_constants.py" AND
   EXISTS "${AOS_RETAIL_SOURCE_ROOT}/aoslib/weapons/list.py")
    add_test(
        NAME aos_weapon_catalog_contract_check
        COMMAND
            "${Python3_EXECUTABLE}"
            "${PROJECT_SOURCE_DIR}/tools/generate_weapon_catalog.py"
            --server-root "${AOS_SIBLING_SERVER_ROOT}"
            --retail-source-root "${AOS_RETAIL_SOURCE_ROOT}"
            --check
    )
    add_test(
        NAME aos_class_catalog_contract_check
        COMMAND
            "${Python3_EXECUTABLE}"
            "${PROJECT_SOURCE_DIR}/tools/generate_class_catalog.py"
            --server-root "${AOS_SIBLING_SERVER_ROOT}"
            --output "${PROJECT_SOURCE_DIR}/src/world/class_catalog.generated.cpp"
            --check
    )
    # The entity catalog needs BOTH trees and BOTH Python dialects: retail is
    # Python 2 and our server has been ported to Python 3, so the check only
    # runs where a Python 2.7 interpreter is also present.
    if(EXISTS "C:/Python27/python.exe" AND
       EXISTS "${AOS_RETAIL_SOURCE_ROOT}/shared/constants.py")
        add_test(
            NAME aos_entity_catalog_contract_check
            COMMAND
                "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/tools/generate_entity_catalog.py"
                --retail "${AOS_RETAIL_SOURCE_ROOT}/shared/constants.py"
                --server "${AOS_SIBLING_SERVER_ROOT}/shared/constants.py"
                --out "${PROJECT_SOURCE_DIR}/src/world/entity_catalog.generated.cpp"
                --check
        )
    endif()
endif()

add_executable(aos_vxl_tests test_vxl.cpp)
target_link_libraries(aos_vxl_tests PRIVATE BattleSpades::World)
target_compile_definitions(aos_vxl_tests PRIVATE
    AOS_TRAINING_VXL="${PROJECT_SOURCE_DIR}/../BattleSpades/maps/Training.vxl")
aos_enable_strict_warnings(aos_vxl_tests)
add_test(NAME aos_vxl_tests COMMAND aos_vxl_tests)

add_executable(
    aos_resource_paths_tests
    test_resource_paths.cpp
)
target_link_libraries(aos_resource_paths_tests PRIVATE BattleSpades::Core)
aos_enable_strict_warnings(aos_resource_paths_tests)
add_test(NAME aos_resource_paths_tests COMMAND aos_resource_paths_tests)

add_executable(
    aos_utf8_tests
    test_utf8.cpp
)
target_link_libraries(aos_utf8_tests PRIVATE BattleSpades::Core)
aos_enable_strict_warnings(aos_utf8_tests)
add_test(NAME aos_utf8_tests COMMAND aos_utf8_tests)

add_executable(
    aos_settings_tests
    test_settings.cpp
)
target_link_libraries(aos_settings_tests PRIVATE BattleSpades::Settings)
aos_enable_strict_warnings(aos_settings_tests)
add_test(NAME aos_settings_tests COMMAND aos_settings_tests)

if(TARGET BattleSpades::RenderBgfx)
    add_executable(
        aos_texture_quality_tests
        test_texture_quality.cpp
    )
    target_link_libraries(aos_texture_quality_tests PRIVATE BattleSpades::RenderBgfx)
    target_compile_definitions(
        aos_texture_quality_tests
        PRIVATE AOS_TEST_ASSET_ROOT="${AOS_ASSET_ROOT}"
    )
    aos_enable_strict_warnings(aos_texture_quality_tests)
    add_test(NAME aos_texture_quality_tests COMMAND aos_texture_quality_tests)
endif()

add_executable(
    aos_ui_tests
    test_ui.cpp
)

target_link_libraries(
    aos_ui_tests
    PRIVATE
        BattleSpades::Ui
)
aos_enable_strict_warnings(aos_ui_tests)

add_test(NAME aos_ui_tests COMMAND aos_ui_tests)

add_executable(aos_ui_customization_tests test_ui_customization.cpp)
target_link_libraries(aos_ui_customization_tests PRIVATE BattleSpades::Frontend)
aos_enable_strict_warnings(aos_ui_customization_tests)
add_test(NAME aos_ui_customization_tests COMMAND aos_ui_customization_tests)

add_executable(
    aos_frontend_tests
    test_frontend.cpp
)

target_link_libraries(
    aos_frontend_tests
    PRIVATE
        BattleSpades::Frontend
)
target_compile_definitions(
    aos_frontend_tests
    PRIVATE
        AOS_TEST_ASSET_ROOT="${AOS_ASSET_ROOT}"
)
aos_enable_strict_warnings(aos_frontend_tests)

add_test(NAME aos_frontend_tests COMMAND aos_frontend_tests)

add_executable(aos_friends_lobby_tests test_friends_lobby.cpp)
target_link_libraries(aos_friends_lobby_tests PRIVATE BattleSpades::Frontend)
aos_enable_strict_warnings(aos_friends_lobby_tests)
add_test(NAME aos_friends_lobby_tests COMMAND aos_friends_lobby_tests)

add_executable(
    aos_join_match_frontend_tests
    test_join_match_frontend.cpp
)
target_link_libraries(aos_join_match_frontend_tests PRIVATE BattleSpades::Frontend)
target_compile_definitions(
    aos_join_match_frontend_tests
    PRIVATE AOS_TEST_ASSET_ROOT="${AOS_ASSET_ROOT}"
)
aos_enable_strict_warnings(aos_join_match_frontend_tests)
add_test(NAME aos_join_match_frontend_tests COMMAND aos_join_match_frontend_tests)

add_executable(
    aos_create_match_menu_tests
    test_create_match_menu.cpp
)
target_link_libraries(aos_create_match_menu_tests PRIVATE BattleSpades::Frontend)
aos_enable_strict_warnings(aos_create_match_menu_tests)
add_test(NAME aos_create_match_menu_tests COMMAND aos_create_match_menu_tests)

add_executable(
    aos_custom_match_frontend_tests
    test_custom_match_frontend.cpp
)
target_link_libraries(aos_custom_match_frontend_tests PRIVATE BattleSpades::Frontend)
target_compile_definitions(
    aos_custom_match_frontend_tests
    PRIVATE AOS_TEST_ASSET_ROOT="${AOS_ASSET_ROOT}"
)
aos_enable_strict_warnings(aos_custom_match_frontend_tests)
add_test(NAME aos_custom_match_frontend_tests COMMAND aos_custom_match_frontend_tests)

add_executable(
    aos_quick_play_frontend_tests
    test_quick_play_frontend.cpp
)
target_link_libraries(aos_quick_play_frontend_tests PRIVATE BattleSpades::Frontend)
target_compile_definitions(
    aos_quick_play_frontend_tests
    PRIVATE AOS_TEST_ASSET_ROOT="${AOS_ASSET_ROOT}"
)
aos_enable_strict_warnings(aos_quick_play_frontend_tests)
add_test(NAME aos_quick_play_frontend_tests COMMAND aos_quick_play_frontend_tests)

add_executable(
    aos_ugc_select_menu_tests
    test_ugc_select_menu.cpp
)
target_link_libraries(aos_ugc_select_menu_tests PRIVATE BattleSpades::Frontend)
target_compile_definitions(
    aos_ugc_select_menu_tests
    PRIVATE AOS_TEST_ASSET_ROOT="${AOS_ASSET_ROOT}"
)
aos_enable_strict_warnings(aos_ugc_select_menu_tests)
add_test(NAME aos_ugc_select_menu_tests COMMAND aos_ugc_select_menu_tests)

add_executable(
    aos_ugc_publish_menu_tests
    test_ugc_publish_menu.cpp
)
target_link_libraries(aos_ugc_publish_menu_tests PRIVATE BattleSpades::Frontend)
target_compile_definitions(
    aos_ugc_publish_menu_tests
    PRIVATE AOS_TEST_ASSET_ROOT="${AOS_ASSET_ROOT}"
)
aos_enable_strict_warnings(aos_ugc_publish_menu_tests)
add_test(NAME aos_ugc_publish_menu_tests COMMAND aos_ugc_publish_menu_tests)

foreach(frontend_test IN ITEMS leaderboard_menu player_profile_menu preload_loading)
    add_executable(
        "aos_${frontend_test}_tests"
        "test_${frontend_test}.cpp"
    )
    target_link_libraries("aos_${frontend_test}_tests" PRIVATE BattleSpades::Frontend)
    aos_enable_strict_warnings("aos_${frontend_test}_tests")
    add_test(NAME "aos_${frontend_test}_tests" COMMAND "aos_${frontend_test}_tests")
endforeach()

add_executable(aos_loading_presentation_tests test_loading_presentation.cpp)
target_link_libraries(aos_loading_presentation_tests PRIVATE BattleSpades::Frontend)
aos_enable_strict_warnings(aos_loading_presentation_tests)
add_test(NAME aos_loading_presentation_tests COMMAND aos_loading_presentation_tests)

add_executable(
    aos_main_menu_presentation_tests
    test_main_menu_presentation.cpp
)

target_link_libraries(
    aos_main_menu_presentation_tests
    PRIVATE
        BattleSpades::Frontend
)
aos_enable_strict_warnings(aos_main_menu_presentation_tests)

add_test(
    NAME aos_main_menu_presentation_tests
    COMMAND aos_main_menu_presentation_tests
)

add_executable(aos_identity_menu_tests test_identity_menu.cpp)
target_link_libraries(aos_identity_menu_tests PRIVATE BattleSpades::Frontend)
aos_enable_strict_warnings(aos_identity_menu_tests)
add_test(NAME aos_identity_menu_tests COMMAND aos_identity_menu_tests)

add_executable(
    aos_resolution_confirmation_tests
    test_resolution_confirmation.cpp
)
target_link_libraries(aos_resolution_confirmation_tests PRIVATE BattleSpades::Frontend)
aos_enable_strict_warnings(aos_resolution_confirmation_tests)
add_test(NAME aos_resolution_confirmation_tests COMMAND aos_resolution_confirmation_tests)

add_executable(
    aos_settings_presentation_tests
    test_settings_presentation.cpp
)
target_link_libraries(aos_settings_presentation_tests PRIVATE BattleSpades::Frontend)
aos_enable_strict_warnings(aos_settings_presentation_tests)
add_test(NAME aos_settings_presentation_tests COMMAND aos_settings_presentation_tests)

add_executable(
    aos_settings_menu_tests
    test_settings_menu.cpp
)
target_link_libraries(aos_settings_menu_tests PRIVATE BattleSpades::Frontend)
aos_enable_strict_warnings(aos_settings_menu_tests)
add_test(NAME aos_settings_menu_tests COMMAND aos_settings_menu_tests)

add_executable(
    aos_frontend_controller_tests
    test_frontend_controller.cpp
)
target_link_libraries(aos_frontend_controller_tests PRIVATE BattleSpades::Frontend)
aos_enable_strict_warnings(aos_frontend_controller_tests)
add_test(NAME aos_frontend_controller_tests COMMAND aos_frontend_controller_tests)

add_executable(
    aos_parity_debug_menu_tests
    test_parity_debug_menu.cpp
)
target_link_libraries(aos_parity_debug_menu_tests PRIVATE BattleSpades::Frontend)
aos_enable_strict_warnings(aos_parity_debug_menu_tests)
add_test(NAME aos_parity_debug_menu_tests COMMAND aos_parity_debug_menu_tests)

add_executable(
    aos_ugc_editor_menu_tests
    test_ugc_editor_menu.cpp
)
target_link_libraries(aos_ugc_editor_menu_tests PRIVATE BattleSpades::Frontend)
target_compile_definitions(
    aos_ugc_editor_menu_tests
    PRIVATE AOS_TEST_ASSET_ROOT="${AOS_ASSET_ROOT}"
)
aos_enable_strict_warnings(aos_ugc_editor_menu_tests)
add_test(NAME aos_ugc_editor_menu_tests COMMAND aos_ugc_editor_menu_tests)

if(AOS_ENABLE_NATIVE_BACKENDS)
    # Imported package targets are directory-scoped unless promoted globally;
    # the platform adapter intentionally keeps SDL private, while this black-
    # box event test needs SDL_PushEvent for deterministic input injection.
    find_package(SDL3 CONFIG REQUIRED)

    add_executable(aos_asset_install_tests test_asset_install.cpp)
    target_link_libraries(aos_asset_install_tests PRIVATE BattleSpades::AssetInstall)
    aos_enable_strict_warnings(aos_asset_install_tests)
    add_test(NAME aos_asset_install_tests COMMAND aos_asset_install_tests)

    add_executable(aos_ugc_preview_writer_tests test_ugc_preview_writer.cpp)
    target_link_libraries(aos_ugc_preview_writer_tests PRIVATE BattleSpades::FrontendNative)
    aos_enable_strict_warnings(aos_ugc_preview_writer_tests)
    add_test(NAME aos_ugc_preview_writer_tests COMMAND aos_ugc_preview_writer_tests)

    add_executable(
        aos_sdl_window_tests
        test_sdl_window.cpp
    )
    target_link_libraries(
        aos_sdl_window_tests
        PRIVATE
            BattleSpades::PlatformSdl
            SDL3::SDL3
    )
    target_compile_definitions(
        aos_sdl_window_tests
        PRIVATE AOS_TEST_ASSET_ROOT="${AOS_ASSET_ROOT}"
    )
    aos_enable_strict_warnings(aos_sdl_window_tests)
    add_test(NAME aos_sdl_window_tests COMMAND aos_sdl_window_tests)

    add_executable(
        aos_text_tests
        test_text_rasterizer.cpp
    )
    target_link_libraries(aos_text_tests PRIVATE BattleSpades::Text)
    target_compile_definitions(
        aos_text_tests
        PRIVATE
            AOS_TEST_ASSET_ROOT="${AOS_ASSET_ROOT}"
    )
    aos_enable_strict_warnings(aos_text_tests)
    add_test(NAME aos_text_tests COMMAND aos_text_tests)

    add_executable(
        aos_protocol168_session_tests
        test_protocol168_session.cpp
    )
    target_link_libraries(
        aos_protocol168_session_tests
        PRIVATE BattleSpades::NetworkEnet
    )
    aos_enable_strict_warnings(aos_protocol168_session_tests)
    add_test(NAME aos_protocol168_session_tests COMMAND aos_protocol168_session_tests)

    add_executable(aos_server_discovery_tests test_server_discovery.cpp)
    target_link_libraries(aos_server_discovery_tests PRIVATE BattleSpades::NetworkEnet)
    aos_enable_strict_warnings(aos_server_discovery_tests)
    add_test(NAME aos_server_discovery_tests COMMAND aos_server_discovery_tests)

    add_executable(aos_aosplay_scores_tests test_aosplay_scores.cpp)
    target_link_libraries(aos_aosplay_scores_tests PRIVATE BattleSpades::NetworkEnet)
    aos_enable_strict_warnings(aos_aosplay_scores_tests)
    add_test(NAME aos_aosplay_scores_tests COMMAND aos_aosplay_scores_tests)

    add_executable(aos_revival_identity_tests test_revival_identity.cpp)
    target_link_libraries(aos_revival_identity_tests PRIVATE BattleSpades::NetworkEnet)
    aos_enable_strict_warnings(aos_revival_identity_tests)
    add_test(NAME aos_revival_identity_tests COMMAND aos_revival_identity_tests)

    add_executable(aos_revival_social_tests test_revival_social.cpp)
    target_link_libraries(aos_revival_social_tests PRIVATE BattleSpades::NetworkEnet)
    aos_enable_strict_warnings(aos_revival_social_tests)
    add_test(NAME aos_revival_social_tests COMMAND aos_revival_social_tests)

    add_executable(aos_local_server_process_tests test_local_server_process.cpp)
    target_link_libraries(aos_local_server_process_tests PRIVATE BattleSpades::LocalHost)
    aos_enable_strict_warnings(aos_local_server_process_tests)
    add_test(NAME aos_local_server_process_tests COMMAND aos_local_server_process_tests)

    # Renders the shipped world shaders into an offscreen target on a real
    # device. Nothing else ties the Color1 encoding in the vertex layout to the
    # `* 255.0` decode in vs_world.sc, and a mismatch there is invisible in
    # unit tests but flattens every face to one shade in game.
    find_package(bgfx CONFIG REQUIRED)
    add_executable(aos_world_vertex_layout_tests test_world_vertex_layout.cpp)
    target_link_libraries(
        aos_world_vertex_layout_tests
        PRIVATE
            BattleSpades::World
            bgfx::bgfx
    )
    # Reaches into src/ on purpose: the layout header is private to the render
    # library because the project keeps bgfx out of include/, and this test has
    # to exercise the shipped layout itself rather than a copy of it.
    target_include_directories(aos_world_vertex_layout_tests PRIVATE "${PROJECT_SOURCE_DIR}/src")
    target_compile_definitions(
        aos_world_vertex_layout_tests
        PRIVATE AOS_SHADER_BIN_ROOT="${PROJECT_SOURCE_DIR}/src/render/shaders/bin"
    )
    aos_enable_strict_warnings(aos_world_vertex_layout_tests)
    add_test(NAME aos_world_vertex_layout_tests COMMAND aos_world_vertex_layout_tests)

    # Renders the aimed viewmodel offscreen and measures where it lands. The
    # recovered draw_sight constants only centre the sight if the KV6 mesher
    # uses retail's voxel convention, and no arithmetic test can see that --
    # it takes real pixels.
    add_executable(aos_ads_sight_render_tests test_ads_sight_render.cpp)
    target_link_libraries(
        aos_ads_sight_render_tests
        PRIVATE
            BattleSpades::World
            bgfx::bgfx
    )
    target_include_directories(aos_ads_sight_render_tests PRIVATE "${PROJECT_SOURCE_DIR}/src")
    target_compile_definitions(
        aos_ads_sight_render_tests
        PRIVATE
            AOS_SHADER_BIN_ROOT="${PROJECT_SOURCE_DIR}/src/render/shaders/bin"
            AOS_TEST_ASSET_ROOT="${AOS_ASSET_ROOT}"
    )
    aos_enable_strict_warnings(aos_ads_sight_render_tests)
    add_test(NAME aos_ads_sight_render_tests COMMAND aos_ads_sight_render_tests)
endif()
