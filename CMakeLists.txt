cmake_minimum_required(VERSION 3.25)

if(POLICY CMP0141)
    cmake_policy(SET CMP0141 NEW)
endif()
if(CMAKE_GENERATOR MATCHES "Ninja")
    set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "Embedded")
endif()

project(Object_Connect VERSION 1.0.0 LANGUAGES CXX)

include(CTest)

if(NOT WIN32)
    message(FATAL_ERROR "Object_Connect requires Windows.")
endif()
if(NOT MSVC)
    message(FATAL_ERROR "Object_Connect requires the Microsoft Visual C++ toolchain.")
endif()
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "Object_Connect must be configured for x64.")
endif()

if(CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_CONFIGURATION_TYPES "Debug;Release" CACHE STRING "Available configurations" FORCE)
elseif(NOT CMAKE_BUILD_TYPE STREQUAL "Debug" AND NOT CMAKE_BUILD_TYPE STREQUAL "Release")
    message(FATAL_ERROR "Single-configuration generators require CMAKE_BUILD_TYPE=Debug or Release.")
endif()

# Match the prebuilt KamataEngine release runtime.
string(REPLACE "/O2" "/O1" CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE}")
string(REPLACE "/Ob2" "/Ob1" CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE}")

set(_default_kamata_engine_root "D:/code/Runtime/KamataEngine")
if(DEFINED ENV{KAMATA_ENGINE_ROOT} AND NOT "$ENV{KAMATA_ENGINE_ROOT}" STREQUAL "")
    set(_default_kamata_engine_root "$ENV{KAMATA_ENGINE_ROOT}")
endif()
set(KAMATA_ENGINE_ROOT "${_default_kamata_engine_root}" CACHE PATH "KamataEngine runtime root")

set(KAMATA_INCLUDE_DIR "${KAMATA_ENGINE_ROOT}/External/KamataEngine/include")
set(DIRECTXTEX_INCLUDE_DIR "${KAMATA_ENGINE_ROOT}/External/DirectXTex/include")
set(IMGUI_INCLUDE_DIR "${KAMATA_ENGINE_ROOT}/External/imgui")
set(OBJECT_CONNECT_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/include")
set(OBJECT_CONNECT_RESOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/NoviceResources")
set(OBJECT_CONNECT_OUTPUT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/target/$<CONFIG>")

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
        message(FATAL_ERROR "Required engine file or directory was not found:\n  ${_required_path}")
    endif()
endforeach()

set(_required_project_resources
    "${OBJECT_CONNECT_RESOURCE_DIR}/white1x1.png"
    "${OBJECT_CONNECT_RESOURCE_DIR}/debugfont.png"
    "${OBJECT_CONNECT_RESOURCE_DIR}/kamata.ico"
    "${OBJECT_CONNECT_RESOURCE_DIR}/data/levels.csv"
    "${OBJECT_CONNECT_RESOURCE_DIR}/data/nodes.csv"
    "${OBJECT_CONNECT_RESOURCE_DIR}/data/maps/first_link.csv"
    "${OBJECT_CONNECT_RESOURCE_DIR}/data/maps/around_block.csv"
    "${OBJECT_CONNECT_RESOURCE_DIR}/data/maps/clot_path.csv"
    "${OBJECT_CONNECT_RESOURCE_DIR}/axis/axis.obj"
    "${OBJECT_CONNECT_RESOURCE_DIR}/axis/axis.mtl"
    "${OBJECT_CONNECT_RESOURCE_DIR}/axis/axis.jpg"
    "${OBJECT_CONNECT_RESOURCE_DIR}/shaders/Flat2DVS.hlsl"
    "${OBJECT_CONNECT_RESOURCE_DIR}/shaders/Flat2DPS.hlsl"
    "${OBJECT_CONNECT_RESOURCE_DIR}/shaders/Obj.hlsli"
    "${OBJECT_CONNECT_RESOURCE_DIR}/shaders/ObjVS.hlsl"
    "${OBJECT_CONNECT_RESOURCE_DIR}/shaders/ObjPS.hlsl"
    "${OBJECT_CONNECT_RESOURCE_DIR}/shaders/Primitive.hlsli"
    "${OBJECT_CONNECT_RESOURCE_DIR}/shaders/PrimitiveVS.hlsl"
    "${OBJECT_CONNECT_RESOURCE_DIR}/shaders/PrimitivePS.hlsl"
    "${OBJECT_CONNECT_RESOURCE_DIR}/shaders/Shape.hlsli"
    "${OBJECT_CONNECT_RESOURCE_DIR}/shaders/ShapeVS.hlsl"
    "${OBJECT_CONNECT_RESOURCE_DIR}/shaders/ShapePS.hlsl"
    "${OBJECT_CONNECT_RESOURCE_DIR}/shaders/ShapeSRGBOutputPS.hlsl"
    "${OBJECT_CONNECT_RESOURCE_DIR}/shaders/Sprite.hlsli"
    "${OBJECT_CONNECT_RESOURCE_DIR}/shaders/SpriteVS.hlsl"
    "${OBJECT_CONNECT_RESOURCE_DIR}/shaders/SpritePS.hlsl"
    "${OBJECT_CONNECT_RESOURCE_DIR}/shaders/SpriteSRGBOutputPS.hlsl"
)
foreach(_required_path IN LISTS _required_project_resources)
    if(NOT EXISTS "${_required_path}")
        message(FATAL_ERROR "Required Object_Connect resource was not found:\n  ${_required_path}")
    endif()
endforeach()

add_library(KamataEngine::KamataEngine STATIC IMPORTED GLOBAL)
set_target_properties(KamataEngine::KamataEngine PROPERTIES
    IMPORTED_CONFIGURATIONS "Debug;Release"
    IMPORTED_LOCATION_DEBUG "${KAMATA_ENGINE_ROOT}/External/KamataEngine/lib/Debug/KamataEngine.lib"
    IMPORTED_LOCATION_RELEASE "${KAMATA_ENGINE_ROOT}/External/KamataEngine/lib/Release/KamataEngine.lib"
    MAP_IMPORTED_CONFIG_MINSIZEREL Release
    MAP_IMPORTED_CONFIG_RELWITHDEBINFO Release
    INTERFACE_INCLUDE_DIRECTORIES "${KAMATA_INCLUDE_DIR}"
)

add_library(DirectXTex::DirectXTex STATIC IMPORTED GLOBAL)
set_target_properties(DirectXTex::DirectXTex PROPERTIES
    IMPORTED_CONFIGURATIONS "Debug;Release"
    IMPORTED_LOCATION_DEBUG "${KAMATA_ENGINE_ROOT}/External/DirectXTex/lib/Debug/DirectXTex.lib"
    IMPORTED_LOCATION_RELEASE "${KAMATA_ENGINE_ROOT}/External/DirectXTex/lib/Release/DirectXTex.lib"
    MAP_IMPORTED_CONFIG_MINSIZEREL Release
    MAP_IMPORTED_CONFIG_RELWITHDEBINFO Release
    INTERFACE_INCLUDE_DIRECTORIES "${DIRECTXTEX_INCLUDE_DIR}"
)

function(configure_object_connect_target target_name)
    target_compile_features(${target_name} PRIVATE cxx_std_20)
    target_compile_options(${target_name} PRIVATE
        /W4
        /WX
        /sdl
        /permissive-
        /utf-8
        $<$<CONFIG:Release>:/Oi>
        $<$<CONFIG:Release>:/Gy>
    )
    set_target_properties(${target_name} PROPERTIES
        CXX_EXTENSIONS OFF
        MSVC_RUNTIME_LIBRARY "$<$<CONFIG:Debug>:MultiThreadedDebugDLL>$<$<CONFIG:Release>:MultiThreaded>"
    )
    if(CMAKE_GENERATOR MATCHES "^Visual Studio")
        target_compile_options(${target_name} PRIVATE /MP $<$<CONFIG:Release>:/Zi>)
    elseif(CMAKE_GENERATOR MATCHES "Ninja")
        target_compile_options(${target_name} PRIVATE $<$<CONFIG:Release>:/Z7>)
    endif()
endfunction()

set(OBJECT_CONNECT_CORE_SOURCES
    src/ObjectConnect/Data/Csv.cpp
    src/ObjectConnect/Data/PuzzleCatalogLoader.cpp
    src/ObjectConnect/Game/GameFlow.cpp
    src/ObjectConnect/Geometry/Geometry2D.cpp
    src/ObjectConnect/Puzzle/PuzzleBoard.cpp
    src/ObjectConnect/Tentacle/BloodTentacle.cpp
    src/ObjectConnect/Tentacle/RibbonStrip.cpp
)

set(OBJECT_CONNECT_RUNTIME_SOURCES
    src/ObjectConnect/Core/Application.cpp
    src/ObjectConnect/Core/FrameTimer.cpp
    src/ObjectConnect/Game/Game.cpp
    src/ObjectConnect/Input/InputSystem.cpp
    src/ObjectConnect/Rendering/GameUiRenderer.cpp
    src/ObjectConnect/Rendering/PuzzleRenderer.cpp
    src/ObjectConnect/Rendering/TextureHandleRegistry.cpp
)

set(OBJECT_CONNECT_ENTRY_SOURCE src/main.cpp)

# Keep the on-disk module layout visible in Visual Studio as well. Core/runtime
# membership stays explicit because the folder name alone does not define engine
# dependencies (for example, GameFlow is headless while Game is runtime-only).
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/src"
    PREFIX "Source Files"
    FILES
        ${OBJECT_CONNECT_CORE_SOURCES}
        ${OBJECT_CONNECT_RUNTIME_SOURCES}
        ${OBJECT_CONNECT_ENTRY_SOURCE}
)

add_library(object_connect_core STATIC ${OBJECT_CONNECT_CORE_SOURCES})
target_include_directories(object_connect_core PUBLIC "${OBJECT_CONNECT_INCLUDE_DIR}")
target_compile_definitions(object_connect_core PRIVATE NOMINMAX)
configure_object_connect_target(object_connect_core)
add_library(ObjectConnect::Core ALIAS object_connect_core)

add_library(object_connect_runtime STATIC ${OBJECT_CONNECT_RUNTIME_SOURCES})
target_include_directories(object_connect_runtime
    PUBLIC "${OBJECT_CONNECT_INCLUDE_DIR}"
    PRIVATE "$<$<CONFIG:Debug>:${IMGUI_INCLUDE_DIR}>"
)
target_link_libraries(object_connect_runtime
    PUBLIC ObjectConnect::Core
    PRIVATE KamataEngine::KamataEngine DirectXTex::DirectXTex d3d12 d3dcompiler dxgi
)
target_compile_definitions(object_connect_runtime PRIVATE
    UNICODE _UNICODE NOMINMAX $<$<CONFIG:Debug>:USE_IMGUI>
)
configure_object_connect_target(object_connect_runtime)
add_library(ObjectConnect::Runtime ALIAS object_connect_runtime)

add_executable(Object_Connect WIN32 ${OBJECT_CONNECT_ENTRY_SOURCE})
target_link_libraries(Object_Connect PRIVATE ObjectConnect::Runtime)
target_compile_definitions(Object_Connect PRIVATE UNICODE _UNICODE NOMINMAX)
target_link_options(Object_Connect PRIVATE
    $<$<CONFIG:Release>:/DEBUG>
    $<$<CONFIG:Release>:/OPT:REF>
    $<$<CONFIG:Release>:/OPT:ICF>
)
configure_object_connect_target(Object_Connect)

set_target_properties(
    object_connect_core object_connect_runtime Object_Connect
    PROPERTIES INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE
)
set_target_properties(Object_Connect PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${OBJECT_CONNECT_OUTPUT_DIR}"
    PDB_OUTPUT_DIRECTORY "${OBJECT_CONNECT_OUTPUT_DIR}"
    VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:Object_Connect>"
)
set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT Object_Connect)

file(GLOB_RECURSE OBJECT_CONNECT_RESOURCE_FILES CONFIGURE_DEPENDS LIST_DIRECTORIES false
    "${OBJECT_CONNECT_RESOURCE_DIR}/*"
)
add_custom_target(Object_Connect_Resources ALL
    COMMAND "${CMAKE_COMMAND}" -E remove_directory "${OBJECT_CONNECT_OUTPUT_DIR}/Resources"
    COMMAND "${CMAKE_COMMAND}" -E copy_directory
            "${OBJECT_CONNECT_RESOURCE_DIR}"
            "${OBJECT_CONNECT_OUTPUT_DIR}/Resources"
    DEPENDS ${OBJECT_CONNECT_RESOURCE_FILES}
    COMMENT "Deploying Object_Connect resources"
    VERBATIM
)
add_dependencies(Object_Connect Object_Connect_Resources)

if(BUILD_TESTING)
    add_executable(Object_Connect_CoreTests
        tests/TestMain.cpp
        tests/TestSupport.hpp
        tests/Data/PuzzleCatalogTests.cpp
        tests/Game/GameFlowTests.cpp
        tests/Geometry/Geometry2DTests.cpp
        tests/Puzzle/PuzzleBoardTests.cpp
        tests/Tentacle/BloodTentacleTests.cpp
        tests/Tentacle/RibbonStripTests.cpp
    )
    target_include_directories(Object_Connect_CoreTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/tests")
    target_link_libraries(Object_Connect_CoreTests PRIVATE ObjectConnect::Core)
    target_compile_definitions(Object_Connect_CoreTests PRIVATE
        NOMINMAX
        OBJECT_CONNECT_TEST_RESOURCE_ROOT="${OBJECT_CONNECT_RESOURCE_DIR}"
    )
    configure_object_connect_target(Object_Connect_CoreTests)
    set_target_properties(Object_Connect_CoreTests PROPERTIES
        INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE
    )
    add_test(NAME Object_Connect.Core COMMAND Object_Connect_CoreTests)
endif()

add_custom_target(run
    COMMAND "$<TARGET_FILE:Object_Connect>"
    WORKING_DIRECTORY "$<TARGET_FILE_DIR:Object_Connect>"
    DEPENDS Object_Connect
    USES_TERMINAL
)
