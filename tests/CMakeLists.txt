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

add_executable(aos_deferred_cleanup_tests test_deferred_cleanup.cpp)
target_link_libraries(aos_deferred_cleanup_tests PRIVATE BattleSpades::Core)
aos_enable_strict_warnings(aos_deferred_cleanup_tests)
add_test(NAME aos_deferred_cleanup_tests COMMAND aos_deferred_cleanup_tests)
set_tests_properties(aos_deferred_cleanup_tests PROPERTIES TIMEOUT 15)

add_executable(aos_hud_layout_tests test_hud_layout.cpp)
target_link_libraries(aos_hud_layout_tests PRIVATE BattleSpades::Frontend)
aos_enable_strict_warnings(aos_hud_layout_tests)
add_test(NAME aos_hud_layout_tests COMMAND aos_hud_layout_tests)

add_executable(aos_game_hud_tests test_game_hud.cpp)
target_link_libraries(aos_game_hud_tests PRIVATE BattleSpades::Frontend BattleSpades::World)
aos_enable_strict_warnings(aos_game_hud_tests)
add_test(NAME aos_game_hud_tests COMMAND aos_game_hud_tests)

add_executable(aos_retail_input_rules_tests test_retail_input_rules.cpp)
target_link_libraries(aos_retail_input_rules_tests PRIVATE BattleSpades::Frontend)
aos_enable_strict_warnings(aos_retail_input_rules_tests)
add_test(NAME aos_retail_input_rules_tests COMMAND aos_retail_input_rules_tests)

add_executable(aos_retail_hud_rules_tests test_retail_hud_rules.cpp)
target_link_libraries(aos_retail_hud_rules_tests PRIVATE BattleSpades::Frontend)
aos_enable_strict_warnings(aos_retail_hud_rules_tests)
add_test(NAME aos_retail_hud_rules_tests COMMAND aos_retail_hud_rules_tests)

add_executable(aos_match_overlays_tests test_match_overlays.cpp)
target_link_libraries(aos_match_overlays_tests PRIVATE BattleSpades::Frontend)
aos_enable_strict_warnings(aos_match_overlays_tests)
add_test(NAME aos_match_overlays_tests COMMAND aos_match_overlays_tests)

add_executable(aos_retail_announcer_tests test_retail_announcer.cpp)
target_link_libraries(aos_retail_announcer_tests PRIVATE BattleSpades::Frontend)
aos_enable_strict_warnings(aos_retail_announcer_tests)
add_test(NAME aos_retail_announcer_tests COMMAND aos_retail_announcer_tests)

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

add_executable(aos_shadow_projection_tests test_shadow_projection.cpp)
target_link_libraries(aos_shadow_projection_tests PRIVATE BattleSpades::World)
aos_enable_strict_warnings(aos_shadow_projection_tests)
add_test(NAME aos_shadow_projection_tests COMMAND aos_shadow_projection_tests)

add_executable(aos_death_camera_tests test_death_camera.cpp)
target_link_libraries(aos_death_camera_tests PRIVATE BattleSpades::Frontend)
aos_enable_strict_warnings(aos_death_camera_tests)
add_test(NAME aos_death_camera_tests COMMAND aos_death_camera_tests)

add_executable(aos_retail_feel_tests test_retail_feel.cpp)
target_link_libraries(aos_retail_feel_tests PRIVATE BattleSpades::Frontend)
aos_enable_strict_warnings(aos_retail_feel_tests)
add_test(NAME aos_retail_feel_tests COMMAND aos_retail_feel_tests)

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

find_package(nlohmann_json CONFIG REQUIRED)
add_executable(aos_retail_movement_core_tests test_retail_movement_core.cpp)
target_link_libraries(aos_retail_movement_core_tests PRIVATE
    BattleSpades::World nlohmann_json::nlohmann_json)
target_compile_definitions(aos_retail_movement_core_tests PRIVATE
    AOS_RETAIL_MOVEMENT_FIXTURE="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/retail_movement_core.json"
    AOS_RETAIL_CLASS_FIXTURE="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/retail_class_movement.json")
aos_enable_strict_warnings(aos_retail_movement_core_tests)
add_test(NAME aos_retail_movement_core_tests COMMAND aos_retail_movement_core_tests)

add_executable(aos_tutorial_script_tests test_tutorial_script.cpp)
target_link_libraries(aos_tutorial_script_tests PRIVATE
    BattleSpades::World nlohmann_json::nlohmann_json)
target_compile_definitions(aos_tutorial_script_tests PRIVATE
    AOS_TUTORIAL_SCRIPT_FIXTURE="${CMAKE_CURRENT_SOURCE_DIR}/data/tutorial_script.json")
aos_enable_strict_warnings(aos_tutorial_script_tests)
add_test(NAME aos_tutorial_script_tests COMMAND aos_tutorial_script_tests)

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
target_link_libraries(aos_protocol168_terrain_tests PRIVATE
    BattleSpades::Protocol nlohmann_json::nlohmann_json)
target_compile_definitions(aos_protocol168_terrain_tests PRIVATE
    AOS_BLOCK_DAMAGE_FOOTPRINT_FIXTURE="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/block_damage_footprints_live.json")
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

add_executable(aos_retail_audio_mix_tests test_retail_audio_mix.cpp)
target_link_libraries(aos_retail_audio_mix_tests PRIVATE BattleSpades::Ports)
aos_enable_strict_warnings(aos_retail_audio_mix_tests)
add_test(NAME aos_retail_audio_mix_tests COMMAND aos_retail_audio_mix_tests)

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

add_executable(aos_retail_menus_tests test_retail_menus.cpp)
target_link_libraries(aos_retail_menus_tests PRIVATE BattleSpades::Frontend BattleSpades::Settings)
aos_enable_strict_warnings(aos_retail_menus_tests)
add_test(NAME aos_retail_menus_tests COMMAND aos_retail_menus_tests)

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
    add_test(
        NAME aos_localization_catalog_tests
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/test_localization_catalog.py"
    )
endif()

add_executable(
    aos_localization_runtime_tests
    test_localization_catalog_runtime.cpp
)
target_link_libraries(aos_localization_runtime_tests PRIVATE BattleSpades::Frontend)
target_compile_definitions(
    aos_localization_runtime_tests
    PRIVATE AOS_TEST_LOCALIZATION_ROOT="${PROJECT_SOURCE_DIR}/config/localization"
)
aos_enable_strict_warnings(aos_localization_runtime_tests)
add_test(NAME aos_localization_runtime_tests COMMAND aos_localization_runtime_tests)
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

    add_executable(aos_retail_event_cues_tests test_retail_event_cues.cpp)
    target_link_libraries(aos_retail_event_cues_tests PRIVATE BattleSpades::AudioOpenAl)
    aos_enable_strict_warnings(aos_retail_event_cues_tests)
    add_test(NAME aos_retail_event_cues_tests COMMAND aos_retail_event_cues_tests)

    add_executable(aos_asset_install_tests test_asset_install.cpp)
    target_link_libraries(aos_asset_install_tests PRIVATE BattleSpades::AssetInstall)
    aos_enable_strict_warnings(aos_asset_install_tests)
    add_test(NAME aos_asset_install_tests COMMAND aos_asset_install_tests)

    add_executable(aos_ugc_preview_writer_tests test_ugc_preview_writer.cpp)
    target_link_libraries(aos_ugc_preview_writer_tests PRIVATE BattleSpades::FrontendNative)
    aos_enable_strict_warnings(aos_ugc_preview_writer_tests)
    add_test(NAME aos_ugc_preview_writer_tests COMMAND aos_ugc_preview_writer_tests)

    add_executable(aos_ugc_round5_tests test_ugc_round5.cpp)
    target_link_libraries(aos_ugc_round5_tests PRIVATE BattleSpades::FrontendNative)
    aos_enable_strict_warnings(aos_ugc_round5_tests)
    add_test(NAME aos_ugc_round5_tests COMMAND aos_ugc_round5_tests)

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

    add_executable(aos_input_script_tests test_input_script.cpp)
    target_link_libraries(aos_input_script_tests PRIVATE BattleSpades::PlatformSdl SDL3::SDL3)
    aos_enable_strict_warnings(aos_input_script_tests)
    add_test(NAME aos_input_script_tests COMMAND aos_input_script_tests)

    add_executable(
        aos_text_tests
        test_text_rasterizer.cpp
    )
    find_package(nlohmann_json CONFIG REQUIRED)
    target_link_libraries(
        aos_text_tests
        PRIVATE
            BattleSpades::Text
            nlohmann_json::nlohmann_json
    )
    target_compile_definitions(
        aos_text_tests
        PRIVATE
            AOS_TEST_ASSET_ROOT="${AOS_ASSET_ROOT}"
            AOS_TEST_CLIENT_ASSET_ROOT="${PROJECT_SOURCE_DIR}/assets/client"
            AOS_TEST_LOCALIZATION_ROOT="${PROJECT_SOURCE_DIR}/config/localization"
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

    find_package(unofficial-enet CONFIG REQUIRED)
    add_executable(aos_connection_recovery_tests test_connection_recovery.cpp)
    target_link_libraries(aos_connection_recovery_tests
        PRIVATE BattleSpades::NetworkEnet unofficial::enet::enet)
    aos_enable_strict_warnings(aos_connection_recovery_tests)
    add_test(NAME aos_connection_recovery_tests COMMAND aos_connection_recovery_tests)
    set_tests_properties(aos_connection_recovery_tests PROPERTIES TIMEOUT 30)

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
    add_executable(aos_inventory_tests test_inventory.cpp)
    target_link_libraries(aos_inventory_tests PRIVATE BattleSpades::FrontendNative BattleSpades::NetworkEnet)
    aos_enable_strict_warnings(aos_inventory_tests)
    add_test(NAME aos_inventory_tests COMMAND aos_inventory_tests
        "${PROJECT_SOURCE_DIR}/assets/original" "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/inventory-receipt-v1.json")
    if(Python3_Interpreter_FOUND)
        add_test(NAME aos_inventory_http_tests COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/test_inventory_http.py" "$<TARGET_FILE:aos_inventory_tests>")
        add_test(NAME aos_inventory_session_http_tests COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/test_inventory_session_http.py" "$<TARGET_FILE:aos_inventory_tests>")
        add_test(NAME aos_hosted_results_http_tests COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/test_hosted_results_http.py" "$<TARGET_FILE:aos_revival_identity_tests>")
        set_tests_properties(aos_hosted_results_http_tests PROPERTIES TIMEOUT 60)
        add_test(NAME aos_workshop_http_tests COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/test_workshop_http.py" "$<TARGET_FILE:aos_revival_identity_tests>")
    endif()

    add_executable(aos_revival_social_tests test_revival_social.cpp)
    target_link_libraries(aos_revival_social_tests PRIVATE BattleSpades::NetworkEnet)
    aos_enable_strict_warnings(aos_revival_social_tests)
    add_test(NAME aos_revival_social_tests COMMAND aos_revival_social_tests)
    # Manual two-identity scenario driver for the loopback AoSPlay social dev
    # server (aos_revival/scripts/social-dev-server.mjs); not a CTest case.
    add_executable(aos_social_live_driver social_live_driver.cpp)
    target_link_libraries(aos_social_live_driver PRIVATE BattleSpades::NetworkEnet)
    aos_enable_strict_warnings(aos_social_live_driver)

    add_executable(aos_local_server_process_tests test_local_server_process.cpp)
    target_link_libraries(aos_local_server_process_tests PRIVATE BattleSpades::LocalHost)
    aos_enable_strict_warnings(aos_local_server_process_tests)
    add_test(NAME aos_local_server_process_tests COMMAND aos_local_server_process_tests)

    add_executable(aos_steam_connect_tests test_steam_connect.cpp)
    target_link_libraries(aos_steam_connect_tests PRIVATE BattleSpades::NativeSteam)
    aos_enable_strict_warnings(aos_steam_connect_tests)
    add_test(NAME aos_steam_connect_tests COMMAND aos_steam_connect_tests)
    if(WIN32)
        # Speaks the bridge protocol without Steam, so the handshake, noise
        # tolerance, retry and join events are tested on every machine.
        add_executable(aos_fake_steam_bridge fake_steam_bridge.cpp)
        target_link_libraries(aos_fake_steam_bridge PRIVATE BattleSpades::BuildOptions)
        aos_enable_strict_warnings(aos_fake_steam_bridge)
        add_dependencies(aos_steam_connect_tests aos_fake_steam_bridge)
        target_compile_definitions(
            aos_steam_connect_tests
            PRIVATE AOS_FAKE_STEAM_BRIDGE="$<TARGET_FILE:aos_fake_steam_bridge>")
    endif()

    find_package(unofficial-sodium CONFIG REQUIRED)
    add_executable(aos_relay_host_tunnel_tests test_relay_host_tunnel.cpp)
    target_link_libraries(aos_relay_host_tunnel_tests PRIVATE
        BattleSpades::LocalHost unofficial-sodium::sodium)
    aos_enable_strict_warnings(aos_relay_host_tunnel_tests)
    add_test(NAME aos_relay_host_tunnel_tests COMMAND aos_relay_host_tunnel_tests)

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
    add_executable(aos_scripted_weapon_tests test_scripted_weapon.cpp)
    target_link_libraries(aos_scripted_weapon_tests PRIVATE BattleSpades::World)
    add_executable(aos_weapon_variant_tests test_weapon_variants.cpp)
    target_link_libraries(aos_weapon_variant_tests PRIVATE BattleSpades::World nlohmann_json::nlohmann_json)
    add_test(NAME aos_weapon_variant_tests COMMAND aos_weapon_variant_tests "${AOS_ASSET_ROOT}")
    set_tests_properties(aos_weapon_variant_tests PROPERTIES WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")
    if(WIN32)
        add_executable(aos_ui_renderer_tests test_ui_renderer.cpp)
        target_link_libraries(aos_ui_renderer_tests PRIVATE
            BattleSpades::FrontendNative BattleSpades::RenderBgfx SDL3::SDL3 bgfx::bgfx)
        target_include_directories(aos_ui_renderer_tests SYSTEM PRIVATE "${Stb_INCLUDE_DIR}")
        target_compile_definitions(aos_ui_renderer_tests PRIVATE
            AOS_SHADER_BIN_ROOT="${PROJECT_SOURCE_DIR}/src/render/shaders/bin")
        aos_enable_strict_warnings(aos_ui_renderer_tests)
        add_test(NAME aos_ui_renderer_tests COMMAND aos_ui_renderer_tests)
        set_tests_properties(aos_ui_renderer_tests PROPERTIES RESOURCE_LOCK gpu TIMEOUT 90)
        add_executable(aos_shadow_stability_render_tests test_shadow_stability_render.cpp)
        target_link_libraries(aos_shadow_stability_render_tests PRIVATE
            BattleSpades::FrontendNative BattleSpades::RenderBgfx BattleSpades::World SDL3::SDL3 bgfx::bgfx)
        target_include_directories(aos_shadow_stability_render_tests SYSTEM PRIVATE "${Stb_INCLUDE_DIR}")
        target_compile_definitions(aos_shadow_stability_render_tests PRIVATE
            AOS_TEST_ASSET_ROOT="${AOS_ASSET_ROOT}"
            AOS_SHADER_BIN_ROOT="${PROJECT_SOURCE_DIR}/src/render/shaders/bin")
        aos_enable_strict_warnings(aos_shadow_stability_render_tests)
        add_test(NAME aos_shadow_stability_render_tests COMMAND aos_shadow_stability_render_tests)
        add_executable(aos_inventory_view_tests test_inventory_view.cpp)
        add_executable(aos_cosmetic_icon_tests test_cosmetic_icons.cpp)
        target_link_libraries(aos_cosmetic_icon_tests PRIVATE BattleSpades::FrontendNative BattleSpades::RenderBgfx BattleSpades::World SDL3::SDL3 bgfx::bgfx)
        target_include_directories(aos_cosmetic_icon_tests SYSTEM PRIVATE "${Stb_INCLUDE_DIR}")
        target_compile_definitions(aos_cosmetic_icon_tests PRIVATE AOS_TEST_ASSET_ROOT="${AOS_ASSET_ROOT}" AOS_SHADER_BIN_ROOT="${PROJECT_SOURCE_DIR}/src/render/shaders/bin")
        add_test(NAME aos_cosmetic_icon_tests COMMAND aos_cosmetic_icon_tests "${AOS_ASSET_ROOT}")
        set_tests_properties(aos_cosmetic_icon_tests PROPERTIES WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")
        target_link_libraries(aos_inventory_view_tests PRIVATE BattleSpades::FrontendNative BattleSpades::RenderBgfx BattleSpades::World SDL3::SDL3 bgfx::bgfx RmlUi::RmlUi)
        target_include_directories(aos_inventory_view_tests SYSTEM PRIVATE "${Stb_INCLUDE_DIR}")
        target_compile_definitions(aos_inventory_view_tests PRIVATE AOS_TEST_ASSET_ROOT="${AOS_ASSET_ROOT}" AOS_SHADER_BIN_ROOT="${PROJECT_SOURCE_DIR}/src/render/shaders/bin")
        add_custom_command(TARGET aos_inventory_view_tests POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different $<TARGET_RUNTIME_DLLS:aos_inventory_view_tests> $<TARGET_FILE_DIR:aos_inventory_view_tests>
            COMMAND_EXPAND_LISTS)
        add_test(NAME aos_inventory_view_tests COMMAND aos_inventory_view_tests "${AOS_ASSET_ROOT}")
        set_tests_properties(aos_inventory_view_tests PROPERTIES WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")
        add_executable(aos_scripted_weapon_images_tests test_scripted_weapon_images.cpp)
        target_link_libraries(aos_scripted_weapon_images_tests PRIVATE
            BattleSpades::FrontendNative BattleSpades::RenderBgfx BattleSpades::World SDL3::SDL3 bgfx::bgfx)
        target_include_directories(aos_scripted_weapon_images_tests SYSTEM PRIVATE "${Stb_INCLUDE_DIR}")
        target_compile_definitions(aos_scripted_weapon_images_tests PRIVATE
            AOS_TEST_ASSET_ROOT="${AOS_ASSET_ROOT}"
            AOS_SHADER_BIN_ROOT="${PROJECT_SOURCE_DIR}/src/render/shaders/bin")
        add_test(NAME aos_scripted_weapon_images_tests COMMAND aos_scripted_weapon_images_tests)
        add_executable(aos_muzzle_flash_render_tests test_muzzle_flash_render.cpp)
        target_link_libraries(aos_muzzle_flash_render_tests PRIVATE
            BattleSpades::FrontendNative BattleSpades::RenderBgfx BattleSpades::World SDL3::SDL3 bgfx::bgfx)
        target_include_directories(aos_muzzle_flash_render_tests SYSTEM PRIVATE "${Stb_INCLUDE_DIR}")
        target_compile_definitions(aos_muzzle_flash_render_tests PRIVATE
            AOS_TEST_ASSET_ROOT="${AOS_ASSET_ROOT}"
            AOS_SHADER_BIN_ROOT="${PROJECT_SOURCE_DIR}/src/render/shaders/bin")
        add_test(NAME aos_muzzle_flash_render_tests COMMAND aos_muzzle_flash_render_tests)
        set_tests_properties(aos_muzzle_flash_render_tests PROPERTIES WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")
        set_tests_properties(
            aos_shadow_stability_render_tests aos_cosmetic_icon_tests aos_inventory_view_tests
            aos_scripted_weapon_images_tests aos_muzzle_flash_render_tests
            PROPERTIES RESOURCE_LOCK gpu)
    endif()
    add_executable(aos_ads_sight_render_tests test_ads_sight_render.cpp)
    target_link_libraries(
        aos_ads_sight_render_tests
        PRIVATE
            BattleSpades::World
            BattleSpades::FrontendNative
            BattleSpades::NetworkEnet
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
    set_tests_properties(aos_ads_sight_render_tests PROPERTIES RESOURCE_LOCK gpu)
    # Named-asset cache: cache-first lookup, worker decode of cold samples and
    # the music/ambience LRU budget. Skips cleanly without an audio device.
    add_executable(aos_audio_cache_tests test_audio_cache.cpp)
    target_link_libraries(aos_audio_cache_tests PRIVATE BattleSpades::AudioOpenAl)
    target_compile_definitions(aos_audio_cache_tests PRIVATE
        AOS_TEST_ASSET_ROOT="${AOS_ASSET_ROOT}")
    aos_enable_strict_warnings(aos_audio_cache_tests)
    add_test(NAME aos_audio_cache_tests COMMAND aos_audio_cache_tests)
    set_tests_properties(aos_audio_cache_tests PROPERTIES RESOURCE_LOCK audio)
    # Weapon.draw_fps / draw_sight muzzle flash for every retail gun with a
    # first-person flash: the capture asserts the hip flash sits at the
    # muzzle (off the optical axis, small) and the aimed flash never
    # blankets the view.
    foreach(aos_flash_tool 7 8 9 10 15 17 18 19 35 36 37 38 53 60 61 62)
        add_test(NAME aos_view_muzzle_flash_capture_${aos_flash_tool}
                 COMMAND aos_ads_sight_render_tests --viewmodel ${aos_flash_tool} --png
                         "${CMAKE_CURRENT_BINARY_DIR}/muzzle_flash_capture")
        set_tests_properties(aos_view_muzzle_flash_capture_${aos_flash_tool}
                             PROPERTIES RESOURCE_LOCK gpu)
    endforeach()
endif()

if(AOS_ENABLE_NATIVE_BACKENDS AND WIN32)
    add_executable(aos_block_shading_render_tests test_block_shading_render.cpp)
    target_link_libraries(aos_block_shading_render_tests PRIVATE
        BattleSpades::FrontendNative BattleSpades::RenderBgfx BattleSpades::World SDL3::SDL3 bgfx::bgfx)
    target_include_directories(aos_block_shading_render_tests SYSTEM PRIVATE "${Stb_INCLUDE_DIR}")
    target_compile_definitions(aos_block_shading_render_tests PRIVATE
        AOS_TEST_ASSET_ROOT="${AOS_ASSET_ROOT}"
        AOS_SHADER_BIN_ROOT="${PROJECT_SOURCE_DIR}/src/render/shaders/bin")
    aos_enable_strict_warnings(aos_block_shading_render_tests)
    add_test(NAME aos_block_shading_render_tests COMMAND aos_block_shading_render_tests)
    set_tests_properties(aos_block_shading_render_tests PROPERTIES RESOURCE_LOCK gpu
        WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}" TIMEOUT 90)
    # Developer tool (not a test): offscreen production-renderer capture of a
    # map at an exact pose, for retail screenshot comparison.
    add_executable(aos_retail_scene_capture "${PROJECT_SOURCE_DIR}/tools/retail_scene_capture.cpp")
    target_link_libraries(aos_retail_scene_capture PRIVATE
        BattleSpades::FrontendNative BattleSpades::RenderBgfx BattleSpades::World SDL3::SDL3 bgfx::bgfx)
    target_include_directories(aos_retail_scene_capture SYSTEM PRIVATE "${Stb_INCLUDE_DIR}")
    target_compile_definitions(aos_retail_scene_capture PRIVATE
        AOS_TOOL_ASSET_ROOT="${AOS_ASSET_ROOT}"
        AOS_TOOL_SHADER_BIN_ROOT="${PROJECT_SOURCE_DIR}/src/render/shaders/bin")
    aos_enable_strict_warnings(aos_retail_scene_capture)
    # Offscreen before/after capture of third-person characters, packs, back
    # intel, classic corpse and spawn flash (docs/RETAIL_PARITY_GAPS round 2).
    add_executable(aos_character_scene_capture
                   "${PROJECT_SOURCE_DIR}/tools/character_scene_capture.cpp")
    target_link_libraries(aos_character_scene_capture PRIVATE
        BattleSpades::FrontendNative BattleSpades::RenderBgfx BattleSpades::World SDL3::SDL3 bgfx::bgfx)
    target_include_directories(aos_character_scene_capture SYSTEM PRIVATE "${Stb_INCLUDE_DIR}")
    target_compile_definitions(aos_character_scene_capture PRIVATE
        AOS_TOOL_ASSET_ROOT="${AOS_ASSET_ROOT}"
        AOS_TOOL_SHADER_BIN_ROOT="${PROJECT_SOURCE_DIR}/src/render/shaders/bin")
    aos_enable_strict_warnings(aos_character_scene_capture)
    add_test(NAME aos_character_scene_capture_smoke
             COMMAND aos_character_scene_capture
                     "out=${CMAKE_CURRENT_BINARY_DIR}/character_scene_capture.png")
    set_tests_properties(aos_character_scene_capture_smoke PROPERTIES RESOURCE_LOCK gpu)
    add_executable(aos_placement_render_tests test_placement_render.cpp)
    target_link_libraries(aos_placement_render_tests PRIVATE
        BattleSpades::FrontendNative BattleSpades::RenderBgfx BattleSpades::World SDL3::SDL3 bgfx::bgfx)
    target_include_directories(aos_placement_render_tests SYSTEM PRIVATE "${Stb_INCLUDE_DIR}")
    target_compile_definitions(aos_placement_render_tests PRIVATE
        AOS_TEST_ASSET_ROOT="${AOS_ASSET_ROOT}"
        AOS_SHADER_BIN_ROOT="${PROJECT_SOURCE_DIR}/src/render/shaders/bin")
    aos_enable_strict_warnings(aos_placement_render_tests)
    add_test(NAME aos_placement_render_tests COMMAND aos_placement_render_tests)
    set_tests_properties(aos_placement_render_tests PROPERTIES RESOURCE_LOCK gpu
        WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")
endif()

add_executable(aos_retail_effects_tests test_retail_effects.cpp)
target_link_libraries(aos_retail_effects_tests PRIVATE BattleSpades::World)
aos_enable_strict_warnings(aos_retail_effects_tests)
add_test(NAME aos_retail_effects_tests COMMAND aos_retail_effects_tests)
