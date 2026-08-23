cmake_minimum_required(VERSION 3.25)

if(POLICY CMP0141)
    cmake_policy(SET CMP0141 NEW)
endif()
if(CMAKE_GENERATOR MATCHES "Ninja")
    set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "Embedded")
endif()

project(Object_FPS VERSION 2.0.0 LANGUAGES CXX)

include(CTest)

if(NOT WIN32)
    message(FATAL_ERROR "Object_FPS requires Windows.")
endif()

if(NOT MSVC)
    message(FATAL_ERROR "Object_FPS requires the Microsoft Visual C++ toolchain.")
endif()

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "Object_FPS must be configured for x64.")
endif()

if(CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_CONFIGURATION_TYPES "Debug;Release" CACHE STRING "Available build configurations" FORCE)
elseif(NOT CMAKE_BUILD_TYPE STREQUAL "Debug" AND NOT CMAKE_BUILD_TYPE STREQUAL "Release")
    message(
        FATAL_ERROR
        "Single-configuration generators require CMAKE_BUILD_TYPE=Debug or Release. "
        "Use the clion-debug or clion-release preset."
    )
endif()

# 既存の KamataEngine ライブラリと元プロジェクトの Release 設定に合わせる。
string(REPLACE "/O2" "/O1" CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE}")
string(REPLACE "/Ob2" "/Ob1" CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE}")

set(_default_kamata_engine_root "D:/code/Runtime/KamataEngine")
if(DEFINED ENV{KAMATA_ENGINE_ROOT} AND NOT "$ENV{KAMATA_ENGINE_ROOT}" STREQUAL "")
    set(_default_kamata_engine_root "$ENV{KAMATA_ENGINE_ROOT}")
endif()

set(
    KAMATA_ENGINE_ROOT
    "${_default_kamata_engine_root}"
    CACHE PATH
    "Root directory of the KamataEngine runtime"
)

set(KAMATA_INCLUDE_DIR "${KAMATA_ENGINE_ROOT}/External/KamataEngine/include")
set(DIRECTXTEX_INCLUDE_DIR "${KAMATA_ENGINE_ROOT}/External/DirectXTex/include")
set(IMGUI_INCLUDE_DIR "${KAMATA_ENGINE_ROOT}/External/imgui")
set(PROJECT_RESOURCE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/NoviceResources")
set(RETROFPS_RUNTIME_OUTPUT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/target/$<CONFIG>")

set(_required_engine_paths
    "${KAMATA_INCLUDE_DIR}/KamataEngine.h"
    "${DIRECTXTEX_INCLUDE_DIR}"
    "${IMGUI_INCLUDE_DIR}/imgui.h"
    "${KAMATA_ENGINE_ROOT}/External/KamataEngine/lib/Debug/KamataEngine.lib"
    "${KAMATA_ENGINE_ROOT}/External/KamataEngine/lib/Release/KamataEngine.lib"
    "${KAMATA_ENGINE_ROOT}/External/DirectXTex/lib/Debug/DirectXTex.lib"
    "${KAMATA_ENGINE_ROOT}/External/DirectXTex/lib/Release/DirectXTex.lib"
)

foreach(_required_path IN LISTS _required_engine_paths)
    if(NOT EXISTS "${_required_path}")
        message(
            FATAL_ERROR
            "Required KamataEngine file or directory was not found:\n"
            "  ${_required_path}\n"
            "Set KAMATA_ENGINE_ROOT to the correct runtime directory."
        )
    endif()
endforeach()

set(_required_project_resources
    "${PROJECT_RESOURCE_SOURCE_DIR}/white1x1.png"
    "${PROJECT_RESOURCE_SOURCE_DIR}/uvChecker.png"
    "${PROJECT_RESOURCE_SOURCE_DIR}/debugfont.png"
    "${PROJECT_RESOURCE_SOURCE_DIR}/shaders/Obj.hlsli"
    "${PROJECT_RESOURCE_SOURCE_DIR}/shaders/ObjVS.hlsl"
    "${PROJECT_RESOURCE_SOURCE_DIR}/shaders/ObjPS.hlsl"
    "${PROJECT_RESOURCE_SOURCE_DIR}/shaders/Primitive.hlsli"
    "${PROJECT_RESOURCE_SOURCE_DIR}/shaders/PrimitiveVS.hlsl"
    "${PROJECT_RESOURCE_SOURCE_DIR}/shaders/PrimitivePS.hlsl"
    "${PROJECT_RESOURCE_SOURCE_DIR}/shaders/Sprite.hlsli"
    "${PROJECT_RESOURCE_SOURCE_DIR}/shaders/SpriteVS.hlsl"
    "${PROJECT_RESOURCE_SOURCE_DIR}/shaders/SpritePS.hlsl"
    "${PROJECT_RESOURCE_SOURCE_DIR}/shaders/Terrain.hlsli"
    "${PROJECT_RESOURCE_SOURCE_DIR}/shaders/TerrainVS.hlsl"
    "${PROJECT_RESOURCE_SOURCE_DIR}/shaders/TerrainPS.hlsl"
    "${PROJECT_RESOURCE_SOURCE_DIR}/maps/mvp_map.txt"
    "${PROJECT_RESOURCE_SOURCE_DIR}/maps/mvp_map_02.txt"
    "${PROJECT_RESOURCE_SOURCE_DIR}/data/enemies.csv"
    "${PROJECT_RESOURCE_SOURCE_DIR}/data/weapons.csv"
    "${PROJECT_RESOURCE_SOURCE_DIR}/data/levels.csv"
    "${PROJECT_RESOURCE_SOURCE_DIR}/cube/cube.obj"
    "${PROJECT_RESOURCE_SOURCE_DIR}/cube/cube.mtl"
    "${PROJECT_RESOURCE_SOURCE_DIR}/cube/cube.jpg"
    "${PROJECT_RESOURCE_SOURCE_DIR}/map_floor/map_floor.obj"
    "${PROJECT_RESOURCE_SOURCE_DIR}/map_floor/map_floor.mtl"
    "${PROJECT_RESOURCE_SOURCE_DIR}/map_wall/map_wall.obj"
    "${PROJECT_RESOURCE_SOURCE_DIR}/map_wall/map_wall.mtl"
)

foreach(_required_path IN LISTS _required_project_resources)
    if(NOT EXISTS "${_required_path}")
        message(FATAL_ERROR "Required project resource was not found:\n  ${_required_path}")
    endif()
endforeach()

add_library(KamataEngine::KamataEngine STATIC IMPORTED GLOBAL)
set_target_properties(
    KamataEngine::KamataEngine
    PROPERTIES
        IMPORTED_CONFIGURATIONS "Debug;Release"
        IMPORTED_LOCATION_DEBUG
            "${KAMATA_ENGINE_ROOT}/External/KamataEngine/lib/Debug/KamataEngine.lib"
        IMPORTED_LOCATION_RELEASE
            "${KAMATA_ENGINE_ROOT}/External/KamataEngine/lib/Release/KamataEngine.lib"
        MAP_IMPORTED_CONFIG_MINSIZEREL Release
        MAP_IMPORTED_CONFIG_RELWITHDEBINFO Release
        INTERFACE_INCLUDE_DIRECTORIES "${KAMATA_INCLUDE_DIR}"
)

add_library(DirectXTex::DirectXTex STATIC IMPORTED GLOBAL)
set_target_properties(
    DirectXTex::DirectXTex
    PROPERTIES
        IMPORTED_CONFIGURATIONS "Debug;Release"
        IMPORTED_LOCATION_DEBUG
            "${KAMATA_ENGINE_ROOT}/External/DirectXTex/lib/Debug/DirectXTex.lib"
        IMPORTED_LOCATION_RELEASE
            "${KAMATA_ENGINE_ROOT}/External/DirectXTex/lib/Release/DirectXTex.lib"
        MAP_IMPORTED_CONFIG_MINSIZEREL Release
        MAP_IMPORTED_CONFIG_RELWITHDEBINFO Release
        INTERFACE_INCLUDE_DIRECTORIES "${DIRECTXTEX_INCLUDE_DIR}"
)

function(configure_object_fps_target target_name)
    target_compile_features(${target_name} PRIVATE cxx_std_20)
    target_compile_options(
        ${target_name}
        PRIVATE
            /W4
            /WX
            /sdl
            /permissive-
            /utf-8
            $<$<CONFIG:Release>:/Oi>
            $<$<CONFIG:Release>:/Gy>
    )
    set_target_properties(
        ${target_name}
        PROPERTIES
            CXX_EXTENSIONS OFF
            MSVC_RUNTIME_LIBRARY
                "$<$<CONFIG:Debug>:MultiThreadedDebugDLL>$<$<CONFIG:Release>:MultiThreaded>"
    )

    if(CMAKE_GENERATOR MATCHES "^Visual Studio")
        target_compile_options(
            ${target_name}
            PRIVATE /MP $<$<CONFIG:Release>:/Zi>
        )
    elseif(CMAKE_GENERATOR MATCHES "Ninja")
        target_compile_options(${target_name} PRIVATE $<$<CONFIG:Release>:/Z7>)
    endif()
endfunction()

set(RETROFPS_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/include")

add_library(retrofps_public_options INTERFACE)
target_compile_features(retrofps_public_options INTERFACE cxx_std_20)
target_include_directories(retrofps_public_options INTERFACE "${RETROFPS_INCLUDE_DIR}")

add_library(retrofps_math INTERFACE)
target_link_libraries(retrofps_math INTERFACE retrofps_public_options)
add_library(RetroFPS::Math ALIAS retrofps_math)

add_library(retrofps_input_contract INTERFACE)
target_link_libraries(retrofps_input_contract INTERFACE retrofps_public_options)
add_library(RetroFPS::InputContract ALIAS retrofps_input_contract)

add_library(
    retrofps_game_flow STATIC
    src/Game/GameFlow.cpp
    include/RetroFPS/Game/GameFlow.hpp
)
target_link_libraries(retrofps_game_flow PUBLIC retrofps_public_options)
target_compile_definitions(retrofps_game_flow PRIVATE NOMINMAX)
configure_object_fps_target(retrofps_game_flow)
add_library(RetroFPS::GameFlow ALIAS retrofps_game_flow)

add_library(
    retrofps_campaign_state STATIC
    src/Game/CampaignRunState.cpp
    include/RetroFPS/Game/CampaignRunState.hpp
)
target_link_libraries(retrofps_campaign_state PUBLIC retrofps_public_options)
target_compile_definitions(retrofps_campaign_state PRIVATE NOMINMAX)
configure_object_fps_target(retrofps_campaign_state)
add_library(RetroFPS::CampaignState ALIAS retrofps_campaign_state)

add_library(
    retrofps_world STATIC
    src/World/GridMap.cpp
    src/World/GridMapLoader.cpp
    src/World/World.cpp
    include/RetroFPS/World/GridMap.hpp
    include/RetroFPS/World/GridMapLoader.hpp
    include/RetroFPS/World/World.hpp
    include/RetroFPS/World/WorldSettings.hpp
    include/RetroFPS/Math/Vector.hpp
)
target_link_libraries(retrofps_world PUBLIC retrofps_math)
target_compile_definitions(retrofps_world PRIVATE NOMINMAX)
configure_object_fps_target(retrofps_world)
add_library(RetroFPS::World ALIAS retrofps_world)

add_library(
    retrofps_data STATIC
    src/Data/Csv.cpp
    src/Data/GameData.cpp
    include/RetroFPS/Data/Csv.hpp
    include/RetroFPS/Data/GameData.hpp
)
target_link_libraries(retrofps_data PUBLIC retrofps_world)
target_compile_definitions(retrofps_data PRIVATE NOMINMAX)
configure_object_fps_target(retrofps_data)
add_library(RetroFPS::Data ALIAS retrofps_data)

add_library(
    retrofps_map_scene STATIC
    src/Game/MapSceneManager.cpp
    include/RetroFPS/Game/MapSceneManager.hpp
)
target_link_libraries(retrofps_map_scene PUBLIC retrofps_world)
target_compile_definitions(retrofps_map_scene PRIVATE NOMINMAX)
configure_object_fps_target(retrofps_map_scene)
add_library(RetroFPS::MapScene ALIAS retrofps_map_scene)

add_library(
    retrofps_collision STATIC
    src/Collision/CombatCollision.cpp
    src/Collision/GridCollision.cpp
    include/RetroFPS/Collision/CombatCollision.hpp
    include/RetroFPS/Collision/GridCollision.hpp
)
target_link_libraries(retrofps_collision PUBLIC retrofps_math retrofps_world)
target_compile_definitions(retrofps_collision PRIVATE NOMINMAX)
configure_object_fps_target(retrofps_collision)
add_library(RetroFPS::Collision ALIAS retrofps_collision)

add_library(
    retrofps_gameplay STATIC
    src/Gameplay/Combat/ProjectileSystem.cpp
    src/Gameplay/Player/PlanarMovement.cpp
    src/Gameplay/Player/Player.cpp
    src/Gameplay/Player/PlayerCombatState.cpp
    src/Gameplay/Player/PlayerController.cpp
    src/Gameplay/Player/PlayerSettings.cpp
    src/Gameplay/Weapon/WeaponController.cpp
    include/RetroFPS/Gameplay/Combat/ProjectileSystem.hpp
    include/RetroFPS/Gameplay/Player/PlanarMovement.hpp
    include/RetroFPS/Gameplay/Player/Player.hpp
    include/RetroFPS/Gameplay/Player/PlayerCombatState.hpp
    include/RetroFPS/Gameplay/Player/PlayerController.hpp
    include/RetroFPS/Gameplay/Player/PlayerSettings.hpp
    include/RetroFPS/Gameplay/Weapon/WeaponController.hpp
    include/RetroFPS/Gameplay/Weapon/WeaponState.hpp
    include/RetroFPS/Input/InputState.hpp
)
target_link_libraries(
    retrofps_gameplay
    PUBLIC
        retrofps_math
        retrofps_input_contract
        retrofps_world
        retrofps_collision
        retrofps_data
)
target_compile_definitions(retrofps_gameplay PRIVATE NOMINMAX)
configure_object_fps_target(retrofps_gameplay)
add_library(RetroFPS::Gameplay ALIAS retrofps_gameplay)

add_library(
    retrofps_enemy_ai STATIC
    src/Gameplay/Enemy/EnemySpawnDirector.cpp
    src/Gameplay/Enemy/EnemySystem.cpp
    include/RetroFPS/Gameplay/Enemy/EnemySpawnDirector.hpp
    include/RetroFPS/Gameplay/Enemy/EnemySystem.hpp
)
target_link_libraries(
    retrofps_enemy_ai
    PUBLIC
        retrofps_math
        retrofps_world
        retrofps_collision
        retrofps_data
)
target_compile_definitions(retrofps_enemy_ai PRIVATE NOMINMAX)
configure_object_fps_target(retrofps_enemy_ai)
add_library(RetroFPS::EnemyAI ALIAS retrofps_enemy_ai)

add_library(
    retrofps_map_geometry STATIC
    src/Rendering/MapGeometryGenerator.cpp
    include/RetroFPS/Rendering/MapGeometry.hpp
    include/RetroFPS/Rendering/MapGeometryGenerator.hpp
)
target_link_libraries(retrofps_map_geometry PUBLIC retrofps_math retrofps_world)
target_compile_definitions(retrofps_map_geometry PRIVATE NOMINMAX)
configure_object_fps_target(retrofps_map_geometry)
add_library(RetroFPS::MapGeometry ALIAS retrofps_map_geometry)

add_library(
    retrofps_input STATIC
    src/Input/InputSystem.cpp
    src/Input/MouseCapture.cpp
    src/Input/MouseCapture.hpp
    include/RetroFPS/Input/InputSystem.hpp
)
target_include_directories(retrofps_input PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(
    retrofps_input
    PUBLIC retrofps_input_contract
    PRIVATE KamataEngine::KamataEngine
)
target_compile_definitions(retrofps_input PRIVATE UNICODE _UNICODE NOMINMAX)
configure_object_fps_target(retrofps_input)
add_library(RetroFPS::Input ALIAS retrofps_input)

add_library(
    retrofps_rendering STATIC
    src/Rendering/EnemyBillboardRenderer.cpp
    src/Rendering/FirstPersonCamera.cpp
    src/Rendering/GameHudRenderer.cpp
    src/Rendering/GameUiRenderer.cpp
    src/Rendering/MapRenderer.cpp
    src/Rendering/ProjectileRenderer.cpp
    src/Rendering/ScreenFadeRenderer.cpp
    include/RetroFPS/Rendering/CameraSettings.hpp
    include/RetroFPS/Rendering/EnemyBillboardRenderer.hpp
    include/RetroFPS/Rendering/EnemyRenderSettings.hpp
    include/RetroFPS/Rendering/FirstPersonCamera.hpp
    include/RetroFPS/Rendering/GameHudRenderer.hpp
    include/RetroFPS/Rendering/GameUiRenderer.hpp
    include/RetroFPS/Rendering/MapRenderAssets.hpp
    include/RetroFPS/Rendering/MapRenderer.hpp
    include/RetroFPS/Rendering/ProjectileRenderer.hpp
    include/RetroFPS/Rendering/ScreenFadeRenderer.hpp
)
target_include_directories(
    retrofps_rendering
    PRIVATE "$<$<CONFIG:Debug>:${IMGUI_INCLUDE_DIR}>"
)
target_link_libraries(
    retrofps_rendering
    PUBLIC retrofps_math retrofps_map_geometry retrofps_enemy_ai retrofps_gameplay
    PRIVATE KamataEngine::KamataEngine DirectXTex::DirectXTex d3dcompiler
)
target_compile_definitions(
    retrofps_rendering
    PRIVATE NOMINMAX $<$<CONFIG:Debug>:USE_IMGUI>
)
configure_object_fps_target(retrofps_rendering)
add_library(RetroFPS::Rendering ALIAS retrofps_rendering)

add_library(
    retrofps_game STATIC
    src/Game/Game.cpp
    include/RetroFPS/Game/Game.hpp
    include/RetroFPS/Game/GameConfig.hpp
)
target_link_libraries(
    retrofps_game
    PUBLIC
        retrofps_gameplay
        retrofps_campaign_state
        retrofps_enemy_ai
        retrofps_game_flow
        retrofps_input
        retrofps_map_scene
        retrofps_rendering
        retrofps_world
)
target_compile_definitions(retrofps_game PRIVATE NOMINMAX)
configure_object_fps_target(retrofps_game)
add_library(RetroFPS::Game ALIAS retrofps_game)

add_library(
    retrofps_application STATIC
    src/Core/Application.cpp
    src/Core/FrameTimer.cpp
    include/RetroFPS/Core/Application.hpp
    include/RetroFPS/Core/FrameTimer.hpp
)
target_include_directories(
    retrofps_application
    PRIVATE "$<$<CONFIG:Debug>:${IMGUI_INCLUDE_DIR}>"
)
target_link_libraries(
    retrofps_application
    PUBLIC retrofps_game
    PRIVATE KamataEngine::KamataEngine DirectXTex::DirectXTex d3dcompiler
)
target_compile_definitions(
    retrofps_application
    PRIVATE UNICODE _UNICODE NOMINMAX $<$<CONFIG:Debug>:USE_IMGUI>
)
configure_object_fps_target(retrofps_application)
add_library(RetroFPS::Application ALIAS retrofps_application)

add_executable(Object_FPS WIN32 main.cpp)
target_link_libraries(Object_FPS PRIVATE retrofps_application)
target_compile_definitions(Object_FPS PRIVATE UNICODE _UNICODE NOMINMAX)
target_link_options(
    Object_FPS
    PRIVATE
        $<$<CONFIG:Release>:/DEBUG>
        $<$<CONFIG:Release>:/OPT:REF>
        $<$<CONFIG:Release>:/OPT:ICF>
)
configure_object_fps_target(Object_FPS)

set_target_properties(
    retrofps_world
    retrofps_collision
    retrofps_map_scene
    retrofps_game_flow
    retrofps_gameplay
    retrofps_enemy_ai
    retrofps_map_geometry
    retrofps_input
    retrofps_rendering
    retrofps_game
    retrofps_application
    Object_FPS
    PROPERTIES INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE
)
set_target_properties(
    Object_FPS
    PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/target/$<CONFIG>"
        PDB_OUTPUT_DIRECTORY "${RETROFPS_RUNTIME_OUTPUT_DIR}"
        VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:Object_FPS>"
)

set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT Object_FPS)

file(
    GLOB_RECURSE RETROFPS_RESOURCE_FILES
    CONFIGURE_DEPENDS
    LIST_DIRECTORIES false
    "${PROJECT_RESOURCE_SOURCE_DIR}/*"
)

add_custom_target(
    Object_FPS_Resources ALL
    COMMAND "${CMAKE_COMMAND}" -E remove_directory "${RETROFPS_RUNTIME_OUTPUT_DIR}/Resources"
    COMMAND
        "${CMAKE_COMMAND}" -E copy_directory
        "${PROJECT_RESOURCE_SOURCE_DIR}"
        "${RETROFPS_RUNTIME_OUTPUT_DIR}/Resources"
    # KamataEngine は各モデルディレクトリ内で MTL のテクスチャを解決する。
    # ソース側で複製せず、デプロイ時に既存のチェッカーテクスチャを再利用する。
    COMMAND
        "${CMAKE_COMMAND}" -E copy_if_different
        "${PROJECT_RESOURCE_SOURCE_DIR}/uvChecker.png"
        "${RETROFPS_RUNTIME_OUTPUT_DIR}/Resources/map_floor/uvChecker.png"
    COMMAND
        "${CMAKE_COMMAND}" -E copy_if_different
        "${PROJECT_RESOURCE_SOURCE_DIR}/uvChecker.png"
        "${RETROFPS_RUNTIME_OUTPUT_DIR}/Resources/map_wall/uvChecker.png"
    COMMAND
        "${CMAKE_COMMAND}" -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/imgui.ini"
        "${RETROFPS_RUNTIME_OUTPUT_DIR}/imgui.ini"
    DEPENDS ${RETROFPS_RESOURCE_FILES} "${CMAKE_CURRENT_SOURCE_DIR}/imgui.ini"
    COMMENT "Deploying KamataEngine resources from NoviceResources"
    VERBATIM
)
add_dependencies(Object_FPS Object_FPS_Resources)

if(BUILD_TESTING)
    add_executable(
        Object_FPS_CoreTests
        tests/TestMain.cpp
        tests/TestSupport.hpp
        tests/World/WorldTests.cpp
        tests/Collision/CollisionTests.cpp
        tests/Collision/CombatCollisionTests.cpp
        tests/Rendering/MapGeometryTests.cpp
        tests/Rendering/EnemyBillboardTests.cpp
        tests/Gameplay/PlayerControllerTests.cpp
        tests/Gameplay/PlayerCombatStateTests.cpp
        tests/Gameplay/WeaponControllerTests.cpp
        tests/Gameplay/ProjectileSystemTests.cpp
        tests/Gameplay/EnemySystemTests.cpp
        tests/Gameplay/EnemySpawnDirectorTests.cpp
        tests/Data/GameDataCatalogTests.cpp
        tests/Game/GameFlowTests.cpp
        tests/Game/MapSceneManagerTests.cpp
        tests/Game/CampaignResourceTests.cpp
        tests/Game/CampaignRunStateTests.cpp
    )
    target_include_directories(
        Object_FPS_CoreTests
        PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/tests"
    )
    target_link_libraries(
        Object_FPS_CoreTests
        PRIVATE
            retrofps_world
            retrofps_data
            retrofps_collision
            retrofps_gameplay
            retrofps_enemy_ai
            retrofps_game_flow
            retrofps_campaign_state
            retrofps_map_scene
            retrofps_map_geometry
    )
    target_compile_definitions(
        Object_FPS_CoreTests
        PRIVATE
            NOMINMAX
            RETROFPS_TEST_RESOURCE_ROOT="${PROJECT_RESOURCE_SOURCE_DIR}"
    )
    configure_object_fps_target(Object_FPS_CoreTests)
    set_target_properties(
        Object_FPS_CoreTests
        PROPERTIES INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE
    )
    add_test(NAME Object_FPS.Core COMMAND Object_FPS_CoreTests)
endif()

add_custom_target(
    run
    COMMAND "$<TARGET_FILE:Object_FPS>"
    WORKING_DIRECTORY "$<TARGET_FILE_DIR:Object_FPS>"
    DEPENDS Object_FPS
    USES_TERMINAL
)
