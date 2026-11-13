cmake_minimum_required(VERSION 3.24)
project(Spyro2PC VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
option(SPYRO_BUILD_DESKTOP "Build Win32/D3D11 ImGui workbench" ON)
include(CTest)

add_library(spyro_core
    src/splats.cpp
    src/assets.cpp
    src/game_build.cpp
    src/spyro3.cpp
    src/yotd_assets.cpp
    src/yotd_scripts.cpp
    src/audio.cpp
    src/npc.cpp
    src/savegame.cpp
    src/frontend.cpp
    src/title.cpp
    src/cinematic.cpp
    src/hud.cpp
    src/upgrade_hud.cpp
    src/upgrade_art.cpp
    src/interface.cpp
    src/enemy.cpp
    src/ripto.cpp
    src/playable_boss.cpp
    src/ripto_autoplay.cpp
    src/gamepad.cpp
    src/runtime.cpp
    src/aerial.cpp
    src/look_mode.cpp
    src/headbash.cpp
    src/destruction.cpp
    src/jumping.cpp
    src/extensions.cpp
    src/elements.cpp
    src/element_powers.cpp src/element_water.cpp src/element_ice.cpp src/overlay_vm.cpp src/gte.cpp src/retail_scripts.cpp
    src/demons.cpp
    src/pc_actors.cpp
    src/spyro_dark.cpp
    src/element_art.cpp
    src/carry_pose.cpp
    src/multiplayer.cpp
    src/swimming.cpp
    src/level.cpp
    src/custom_level.cpp
    src/sky.cpp
    src/spyro_model.cpp
    src/spyro_animation.cpp
    src/supercharge.cpp
    src/powerup_effects.cpp
    src/powerup_import.cpp
    src/powerups.cpp
    src/universe.cpp
    src/portal.cpp
    src/travel.cpp
    src/terrain.cpp
    src/scene_animation.cpp
    src/level_systems.cpp
    src/speedway.cpp
    src/breeze.cpp
    src/campaign.cpp
    src/camera.cpp
    src/objects.cpp
    src/object_animation.cpp
    src/retail_math.cpp
    src/companion.cpp
    src/object_interaction.cpp
    src/flame.cpp
)
target_include_directories(spyro_core PUBLIC include)
if(MSVC)
    target_compile_options(spyro_core PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(spyro_core PRIVATE -Wall -Wextra -Wpedantic)
endif()
add_executable(spyro_inspect src/inspect.cpp)
target_link_libraries(spyro_inspect PRIVATE spyro_core)
add_executable(spyro_disc_extract EXCLUDE_FROM_ALL tools/disc_extract.cpp src/assets.cpp src/game_build.cpp)
target_include_directories(spyro_disc_extract PRIVATE include)
add_executable(spyro3_scene_probe EXCLUDE_FROM_ALL tools/spyro3_scene_probe.cpp)
target_link_libraries(spyro3_scene_probe PRIVATE spyro_core)
add_executable(spyro3_model_probe EXCLUDE_FROM_ALL tools/spyro3_model_probe.cpp)
target_link_libraries(spyro3_model_probe PRIVATE spyro_core)
add_executable(spyro3_vm_probe EXCLUDE_FROM_ALL tools/spyro3_vm_probe.cpp src/overlay_vm.cpp src/gte.cpp)
target_include_directories(spyro3_vm_probe PRIVATE include)
add_executable(spyro_build_profile_probe EXCLUDE_FROM_ALL tools/build_profile_probe.cpp)
target_link_libraries(spyro_build_profile_probe PRIVATE spyro_core)
add_executable(spyro_animation_probe src/animation_probe.cpp)
target_link_libraries(spyro_animation_probe PRIVATE spyro_core)
add_executable(spyro_ripto_probe EXCLUDE_FROM_ALL tools/ripto_probe.cpp)
target_link_libraries(spyro_ripto_probe PRIVATE spyro_core)
add_executable(spyro_audio_probe src/audio_probe.cpp)
target_link_libraries(spyro_audio_probe PRIVATE spyro_core)
add_executable(spyro_cinematic_probe tools/cinematic_probe.cpp)
target_link_libraries(spyro_cinematic_probe PRIVATE spyro_core)
add_executable(spyro_cut_content_probe EXCLUDE_FROM_ALL tools/cut_content_probe.cpp)
target_link_libraries(spyro_cut_content_probe PRIVATE spyro_core)
add_executable(spyro_eye_probe EXCLUDE_FROM_ALL tools/probe_spyro_eyes.cpp)
target_link_libraries(spyro_eye_probe PRIVATE spyro_core)
add_executable(spyro_interface_probe EXCLUDE_FROM_ALL tools/probe_interface_sprites.cpp)
add_executable(spyro_retail_text_plate EXCLUDE_FROM_ALL tools/retail_text_plate.cpp)
target_link_libraries(spyro_retail_text_plate PRIVATE spyro_core)
add_executable(spyro_vm_probe EXCLUDE_FROM_ALL tools/vm_probe.cpp)
add_executable(spyro_scene_export EXCLUDE_FROM_ALL tools/scene_export.cpp)
target_link_libraries(spyro_scene_export PRIVATE spyro_core)
add_executable(spyro_script_census EXCLUDE_FROM_ALL tools/script_census.cpp)
add_executable(spyro_script_engagement EXCLUDE_FROM_ALL tools/script_engagement.cpp)
add_executable(spyro_script_quests EXCLUDE_FROM_ALL tools/script_quests.cpp)
target_link_libraries(spyro_script_quests PRIVATE spyro_core)
target_link_libraries(spyro_script_engagement PRIVATE spyro_core)
add_executable(spyro_level_exits EXCLUDE_FROM_ALL tools/level_exits.cpp)
target_link_libraries(spyro_level_exits PRIVATE spyro_core)
add_executable(spyro_level_walk EXCLUDE_FROM_ALL tools/level_walk.cpp)
target_link_libraries(spyro_level_walk PRIVATE spyro_core)
add_executable(spyro_challenges EXCLUDE_FROM_ALL tools/challenges.cpp)
target_link_libraries(spyro_challenges PRIVATE spyro_core)

add_executable(spyro_level_completion EXCLUDE_FROM_ALL tools/level_completion.cpp)
target_link_libraries(spyro_level_completion PRIVATE spyro_core)
add_executable(spyro_open_spots EXCLUDE_FROM_ALL tools/open_spots.cpp)
target_link_libraries(spyro_open_spots PRIVATE spyro_core)
add_executable(spyro_boss_fights EXCLUDE_FROM_ALL tools/boss_fights.cpp)
target_link_libraries(spyro_boss_fights PRIVATE spyro_core)
add_executable(spyro_solid_census EXCLUDE_FROM_ALL tools/solid_census.cpp)
target_link_libraries(spyro_solid_census PRIVATE spyro_core)
add_executable(spyro_ripto_autoplay EXCLUDE_FROM_ALL tools/ripto_autoplay.cpp)
target_link_libraries(spyro_ripto_autoplay PRIVATE spyro_core)
add_executable(spyro_enemy_audit EXCLUDE_FROM_ALL tools/enemy_audit.cpp)
target_link_libraries(spyro_enemy_audit PRIVATE spyro_core)
target_link_libraries(spyro_script_census PRIVATE spyro_core)
target_link_libraries(spyro_vm_probe PRIVATE spyro_core)
target_link_libraries(spyro_interface_probe PRIVATE spyro_core)
add_executable(spyro_level_asset_export EXCLUDE_FROM_ALL tools/level_asset_export.cpp)
target_link_libraries(spyro_level_asset_export PRIVATE spyro_core)
add_executable(spyro_custom_level_tests EXCLUDE_FROM_ALL tests/custom_level_tests.cpp)
target_link_libraries(spyro_custom_level_tests PRIVATE spyro_core)
add_executable(spyro_custom_homeworld_tests EXCLUDE_FROM_ALL tests/custom_homeworld_tests.cpp)
target_link_libraries(spyro_custom_homeworld_tests PRIVATE spyro_core)
add_executable(spyro_spring_realm_tests EXCLUDE_FROM_ALL tests/spring_realm_tests.cpp)
target_link_libraries(spyro_spring_realm_tests PRIVATE spyro_core)
add_executable(spyro_movement_bank_tests EXCLUDE_FROM_ALL tests/movement_bank_tests.cpp)
target_link_libraries(spyro_movement_bank_tests PRIVATE spyro_core)

if(SPYRO_BUILD_DESKTOP AND WIN32)
    include(FetchContent)
    # Pinned Dear ImGui v1.92.5-docking. Runtime never downloads game assets.
    FetchContent_Declare(imgui
        GIT_REPOSITORY https://github.com/ocornut/imgui.git
        GIT_TAG af84778eeaab75bee0b54420f3157cf3ee00618f
        GIT_SHALLOW FALSE
    )
    FetchContent_MakeAvailable(imgui)
    add_library(spyro_imgui STATIC
        ${imgui_SOURCE_DIR}/imgui.cpp ${imgui_SOURCE_DIR}/imgui_draw.cpp
        ${imgui_SOURCE_DIR}/imgui_tables.cpp ${imgui_SOURCE_DIR}/imgui_widgets.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_win32.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_dx11.cpp
    )
    target_include_directories(spyro_imgui PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
    target_compile_definitions(spyro_imgui PUBLIC UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
    # The controller drives only the game (src/gamepad.cpp); ImGui's backend
    # never polls it, so it cannot navigate or change workbench panels.
    target_compile_definitions(spyro_imgui PRIVATE IMGUI_IMPL_WIN32_DISABLE_GAMEPAD)
    target_link_libraries(spyro_imgui PUBLIC d3d11 dxgi d3dcompiler dwmapi)
    add_executable(spyro_workbench WIN32 src/desktop.cpp src/renderer.cpp src/renderer_splats.cpp src/audio_output.cpp src/video_capture.cpp src/online_client.cpp)
    target_link_libraries(spyro_workbench PRIVATE spyro_core spyro_imgui shell32 comdlg32 ole32 uuid winmm xaudio2 winhttp bcrypt)
    target_compile_options(spyro_workbench PRIVATE /W4 /utf-8)
    add_executable(spyro_audio_playback_probe src/audio_playback_probe.cpp src/audio_output.cpp)
    target_link_libraries(spyro_audio_playback_probe PRIVATE spyro_core xaudio2 ole32)
    target_compile_definitions(spyro_audio_playback_probe PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

if(BUILD_TESTING)
    add_executable(spyro_spyro3_tests tests/spyro3_tests.cpp)
    target_link_libraries(spyro_spyro3_tests PRIVATE spyro_core)
    if(EXISTS "${CMAKE_SOURCE_DIR}/data-yotd/SCUS_944.67")
        add_test(NAME spyro3_provisional COMMAND spyro_spyro3_tests "${CMAKE_SOURCE_DIR}/data-yotd" "${CMAKE_SOURCE_DIR}/data/disc")
    endif()
    add_executable(spyro_splat_asset_tests tests/splat_asset_tests.cpp)
    target_link_libraries(spyro_splat_asset_tests PRIVATE spyro_core)
    add_test(NAME authored_scenery_assets COMMAND spyro_splat_asset_tests)
    add_executable(spyro_crownworks_tunnel_tests tests/crownworks_tunnel_tests.cpp)
    target_link_libraries(spyro_crownworks_tunnel_tests PRIVATE spyro_core)
    add_test(NAME crownworks_swim_tunnels COMMAND spyro_crownworks_tunnel_tests "${CMAKE_SOURCE_DIR}/data/disc" "${CMAKE_SOURCE_DIR}/data/custom-levels/cyborg-ripto-arena/pc/crownworks.s2world")
    add_executable(spyro_ripto_tests tests/ripto_tests.cpp)
    target_link_libraries(spyro_ripto_tests PRIVATE spyro_core)
    add_test(NAME ripto_boss COMMAND spyro_ripto_tests "${CMAKE_SOURCE_DIR}/data/disc" "${CMAKE_SOURCE_DIR}/data/custom-levels/cyborg-ripto-arena/pc/crownworks.s2world")
    add_executable(spyro_element_tests tests/element_tests.cpp)
    target_link_libraries(spyro_element_tests PRIVATE spyro_core)
    add_test(NAME elemental_powers COMMAND spyro_element_tests "${CMAKE_SOURCE_DIR}/data/disc")
    add_executable(spyro_retail_script_tests tests/retail_script_tests.cpp)
    target_link_libraries(spyro_retail_script_tests PRIVATE spyro_core)
    add_test(NAME retail_overlay_scripts COMMAND spyro_retail_script_tests "${CMAKE_SOURCE_DIR}/data/disc")
    add_executable(spyro_shockwave_tests tests/shockwave_tests.cpp)
    target_link_libraries(spyro_shockwave_tests PRIVATE spyro_core)
    add_test(NAME elemental_shockwaves COMMAND spyro_shockwave_tests "${CMAKE_SOURCE_DIR}/data/disc")
    add_executable(spyro_demon_tests tests/demon_tests.cpp)
    target_link_libraries(spyro_demon_tests PRIVATE spyro_core)
    add_test(NAME convexity_demons COMMAND spyro_demon_tests)
    add_executable(spyro_headbash_tests tests/headbash_tests.cpp)
    target_link_libraries(spyro_headbash_tests PRIVATE spyro_core)
    add_test(NAME headbash_terrain COMMAND spyro_headbash_tests "${CMAKE_SOURCE_DIR}/data/disc")
    add_executable(spyro_destruction_tests tests/destruction_tests.cpp)
    target_link_libraries(spyro_destruction_tests PRIVATE spyro_core)
    add_test(NAME dynamic_terrain_destruction COMMAND spyro_destruction_tests "${CMAKE_SOURCE_DIR}/data/disc")
    add_executable(spyro_upgrade_hud_tests tests/upgrade_hud_tests.cpp)
    target_link_libraries(spyro_upgrade_hud_tests PRIVATE spyro_core)
    add_test(NAME upgrade_hud COMMAND spyro_upgrade_hud_tests)
    add_executable(spyro_supercharge_tests tests/supercharge_tests.cpp)
    add_executable(spyro_prerelease_tests tests/prerelease_tests.cpp)
    target_link_libraries(spyro_prerelease_tests PRIVATE spyro_core)
    add_test(NAME prerelease_builds_and_roll COMMAND spyro_prerelease_tests "${CMAKE_SOURCE_DIR}")
    set_tests_properties(prerelease_builds_and_roll PROPERTIES SKIP_RETURN_CODE 77)
    add_executable(spyro_prerelease_audio_tests tests/prerelease_audio_tests.cpp)
    target_link_libraries(spyro_prerelease_audio_tests PRIVATE spyro_core)
    add_test(NAME prerelease_spyro_audio COMMAND spyro_prerelease_audio_tests "${CMAKE_SOURCE_DIR}")
    set_tests_properties(prerelease_spyro_audio PROPERTIES SKIP_RETURN_CODE 77)
    add_executable(spyro_prerelease_power_tests tests/prerelease_power_tests.cpp)
    target_link_libraries(spyro_prerelease_power_tests PRIVATE spyro_core)
    add_test(NAME prerelease_retail_powers COMMAND spyro_prerelease_power_tests "${CMAKE_SOURCE_DIR}")
    set_tests_properties(prerelease_retail_powers PROPERTIES SKIP_RETURN_CODE 77)
    target_link_libraries(spyro_supercharge_tests PRIVATE spyro_core)
    add_test(NAME supercharge_wing_variants COMMAND spyro_supercharge_tests "${CMAKE_SOURCE_DIR}/data/disc")
    add_executable(spyro_level_system_tests tests/level_system_tests.cpp)
    target_link_libraries(spyro_level_system_tests PRIVATE spyro_core)
    add_test(NAME retail_level_systems COMMAND spyro_level_system_tests "${CMAKE_SOURCE_DIR}/data/disc")
    add_executable(spyro_portal_tests tests/portal_tests.cpp)
    target_link_libraries(spyro_portal_tests PRIVATE spyro_core)
    add_test(NAME portals_and_whirlwinds COMMAND spyro_portal_tests)
    add_executable(spyro_look_tests tests/look_tests.cpp)
    target_link_libraries(spyro_look_tests PRIVATE spyro_core)
    add_test(NAME look_mode_and_camera COMMAND spyro_look_tests)
    add_executable(spyro_gamepad_tests tests/gamepad_tests.cpp)
    target_link_libraries(spyro_gamepad_tests PRIVATE spyro_core)
    add_test(NAME controller_input COMMAND spyro_gamepad_tests)
    add_executable(spyro_interface_tests tests/interface_tests.cpp)
    target_link_libraries(spyro_interface_tests PRIVATE spyro_core)
    add_test(NAME retail_interface COMMAND spyro_interface_tests)
    add_executable(spyro_companion_tests tests/companion_tests.cpp)
    target_link_libraries(spyro_companion_tests PRIVATE spyro_core)
    add_test(NAME sparx_companion COMMAND spyro_companion_tests)
    add_executable(spyro_jump_tests tests/jump_tests.cpp)
    target_link_libraries(spyro_jump_tests PRIVATE spyro_core)
    add_test(NAME retail_jump_rules COMMAND spyro_jump_tests)
    add_executable(spyro_frontend_tests tests/frontend_tests.cpp)
    target_link_libraries(spyro_frontend_tests PRIVATE spyro_core)
    add_test(NAME frontend_preferences COMMAND spyro_frontend_tests)
    add_executable(spyro_level_behaviour_tests tests/level_behaviour_tests.cpp)
    target_link_libraries(spyro_level_behaviour_tests PRIVATE spyro_core)
    add_test(NAME level_behaviours COMMAND spyro_level_behaviour_tests)
    add_executable(spyro_sky_tests tests/sky_tests.cpp)
    target_link_libraries(spyro_sky_tests PRIVATE spyro_core)
    add_test(NAME retail_skies COMMAND spyro_sky_tests)
    add_executable(spyro_aerial_tests tests/aerial_tests.cpp)
    target_link_libraries(spyro_aerial_tests PRIVATE spyro_core)
    add_test(NAME aerial_and_sand COMMAND spyro_aerial_tests)
    add_executable(spyro_object_material_tests tests/object_material_tests.cpp)
    target_link_libraries(spyro_object_material_tests PRIVATE spyro_core)
    add_test(NAME object_materials COMMAND spyro_object_material_tests)
    add_executable(spyro_extension_tests tests/extensions_tests.cpp)
    target_link_libraries(spyro_extension_tests PRIVATE spyro_core)
    add_test(NAME pc_extensions COMMAND spyro_extension_tests)
    add_executable(spyro_universe_tests tests/universe_tests.cpp)
    target_link_libraries(spyro_universe_tests PRIVATE spyro_core)
    add_test(NAME procedural_universe COMMAND spyro_universe_tests)
    add_executable(spyro_powerup_tests tests/powerup_tests.cpp)
    target_link_libraries(spyro_powerup_tests PRIVATE spyro_core)
    add_test(NAME powerups COMMAND spyro_powerup_tests)
    add_executable(spyro_progression_tests tests/progression_tests.cpp)
    target_link_libraries(spyro_progression_tests PRIVATE spyro_core)
    add_test(NAME game_progression COMMAND spyro_progression_tests)
    add_executable(spyro_npc_tests tests/npc_tests.cpp)
    target_link_libraries(spyro_npc_tests PRIVATE spyro_core)
    add_test(NAME npc_interactions COMMAND spyro_npc_tests)
    add_executable(spyro_campaign_tests tests/campaign_tests.cpp)
    target_link_libraries(spyro_campaign_tests PRIVATE spyro_core)
    add_test(NAME campaign_commands COMMAND spyro_campaign_tests)
    add_executable(spyro_audio_tests tests/audio_tests.cpp)
    target_link_libraries(spyro_audio_tests PRIVATE spyro_core)
    add_test(NAME audio_codecs COMMAND spyro_audio_tests)
    add_executable(spyro_tests tests/core_tests.cpp)
    target_link_libraries(spyro_tests PRIVATE spyro_core)
    add_test(NAME core COMMAND spyro_tests)
    add_executable(spyro_interaction_tests tests/object_interaction_tests.cpp)
    target_link_libraries(spyro_interaction_tests PRIVATE spyro_core)
    add_test(NAME object_interactions COMMAND spyro_interaction_tests)
    add_executable(spyro_secondary_tests tests/secondary_tests.cpp)
    target_link_libraries(spyro_secondary_tests PRIVATE spyro_core)
    add_test(NAME secondary_animation COMMAND spyro_secondary_tests)
    add_executable(spyro_swimming_tests tests/swimming_tests.cpp)
    target_link_libraries(spyro_swimming_tests PRIVATE spyro_core)
    add_test(NAME swimming COMMAND spyro_swimming_tests)
    add_executable(spyro_traversal_tests tests/traversal_tests.cpp)
    target_link_libraries(spyro_traversal_tests PRIVATE spyro_core)
    add_test(NAME traversal COMMAND spyro_traversal_tests)
    add_executable(spyro_terrain_tests tests/terrain_tests.cpp)
    target_link_libraries(spyro_terrain_tests PRIVATE spyro_core)
    add_test(NAME terrain_materials COMMAND spyro_terrain_tests)
    if(SPYRO_BUILD_DESKTOP AND WIN32)
        add_executable(spyro_character_shadow_jar_tests EXCLUDE_FROM_ALL tests/character_shadow_jar_tests.cpp src/renderer.cpp src/renderer_splats.cpp)
        target_link_libraries(spyro_character_shadow_jar_tests PRIVATE spyro_core spyro_imgui)
        add_test(NAME character_shadows_and_life_jars COMMAND spyro_character_shadow_jar_tests "${CMAKE_SOURCE_DIR}")
        add_executable(spyro_renderer_tests tests/renderer_tests.cpp src/renderer.cpp src/renderer_splats.cpp)
        target_link_libraries(spyro_renderer_tests PRIVATE spyro_core spyro_imgui)
        add_test(NAME terrain_renderer COMMAND spyro_renderer_tests "${CMAKE_SOURCE_DIR}/data/disc")
    endif()
    find_package(Python3 COMPONENTS Interpreter)
    if(Python3_FOUND)
        add_test(NAME disc_python COMMAND ${Python3_EXECUTABLE} -m unittest discover -s tests -p test_*.py)
        set_tests_properties(disc_python PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            ENVIRONMENT "SPYRO_INSPECT=$<TARGET_FILE:spyro_inspect>")
    endif()
endif()
