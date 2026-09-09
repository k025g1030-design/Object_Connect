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

set(KAMATA_ENGINE_ROOT "" CACHE PATH
    "KamataEngine runtime root (defaults to the KAMATA_ENGINE environment variable)"
)
if(KAMATA_ENGINE_ROOT STREQUAL "")
    if(DEFINED ENV{KAMATA_ENGINE} AND NOT "$ENV{KAMATA_ENGINE}" STREQUAL "")
        set(KAMATA_ENGINE_ROOT "$ENV{KAMATA_ENGINE}")
    else()
        message(FATAL_ERROR
            "KamataEngine could not be located. Set the KAMATA_ENGINE environment "
            "variable, or configure with -DKAMATA_ENGINE_ROOT=<path>."
        )
    endif()
endif()
cmake_path(ABSOLUTE_PATH KAMATA_ENGINE_ROOT NORMALIZE
    BASE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    OUTPUT_VARIABLE KAMATA_ENGINE_ROOT
)

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
    "${IMGUI_INCLUDE_DIR}/imstb_truetype.h"
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

# The DirectX debug layer loads DXC at runtime. An IDE toolchain may expose an
# SDK copy through PATH, but every launch mode must use the redistributable pair
# from the Windows SDK instead of relying on an IDE-specific environment.
set(_windows_kits_hints)
if(DEFINED CMAKE_WINDOWS_KITS_10_DIR
   AND NOT "${CMAKE_WINDOWS_KITS_10_DIR}" STREQUAL "")
    cmake_path(CONVERT "${CMAKE_WINDOWS_KITS_10_DIR}" TO_CMAKE_PATH_LIST
        _windows_kits_cmake_hints NORMALIZE
    )
    list(APPEND _windows_kits_hints ${_windows_kits_cmake_hints})
endif()
if(DEFINED CMAKE_RC_COMPILER AND IS_ABSOLUTE "${CMAKE_RC_COMPILER}")
    cmake_path(GET CMAKE_RC_COMPILER PARENT_PATH _windows_sdk_arch_bin)
    cmake_path(GET _windows_sdk_arch_bin PARENT_PATH _windows_sdk_version_bin)
    cmake_path(GET _windows_sdk_version_bin PARENT_PATH _windows_sdk_bin)
    cmake_path(GET _windows_sdk_bin PARENT_PATH _windows_sdk_from_rc)
    list(APPEND _windows_kits_hints "${_windows_sdk_from_rc}")
endif()
if(DEFINED ENV{CMAKE_WINDOWS_KITS_10_DIR}
   AND NOT "$ENV{CMAKE_WINDOWS_KITS_10_DIR}" STREQUAL "")
    cmake_path(CONVERT "$ENV{CMAKE_WINDOWS_KITS_10_DIR}" TO_CMAKE_PATH_LIST
        _windows_kits_environment_hints NORMALIZE
    )
    list(APPEND _windows_kits_hints ${_windows_kits_environment_hints})
endif()
if(DEFINED ENV{WindowsSdkDir} AND NOT "$ENV{WindowsSdkDir}" STREQUAL "")
    cmake_path(CONVERT "$ENV{WindowsSdkDir}" TO_CMAKE_PATH_LIST
        _windows_sdk_environment_hints NORMALIZE
    )
    list(APPEND _windows_kits_hints ${_windows_sdk_environment_hints})
endif()
cmake_host_system_information(
    RESULT _windows_kits_registry
    QUERY WINDOWS_REGISTRY
          "HKLM/SOFTWARE/Microsoft/Windows Kits/Installed Roots"
          VALUE "KitsRoot10"
          VIEW 64_32
          ERROR_VARIABLE _windows_kits_registry_error
)
if(_windows_kits_registry)
    foreach(_windows_kits_registry_path IN LISTS _windows_kits_registry)
        cmake_path(CONVERT "${_windows_kits_registry_path}" TO_CMAKE_PATH_LIST
            _windows_kits_registry_hints NORMALIZE
        )
        list(APPEND _windows_kits_hints ${_windows_kits_registry_hints})
    endforeach()
endif()
list(REMOVE_DUPLICATES _windows_kits_hints)

find_path(OBJECT_CONNECT_DXC_REDIST_DIR
    NAMES dxcompiler.dll
    HINTS ${_windows_kits_hints}
    PATH_SUFFIXES "Redist/D3D/x64"
    NO_DEFAULT_PATH
    DOC "Windows SDK x64 DirectX Shader Compiler redistributable directory"
)
if(NOT OBJECT_CONNECT_DXC_REDIST_DIR)
    message(FATAL_ERROR
        "The Windows SDK x64 DirectX Shader Compiler redistributable directory "
        "was not found. Install a Windows 10/11 SDK with the D3D/DXC "
        "redistributable files, or configure with "
        "-DOBJECT_CONNECT_DXC_REDIST_DIR=<path>."
    )
endif()
message(STATUS "Using Windows SDK DXC runtime: ${OBJECT_CONNECT_DXC_REDIST_DIR}")
set(OBJECT_CONNECT_DXC_RUNTIME_FILES
    "${OBJECT_CONNECT_DXC_REDIST_DIR}/dxcompiler.dll"
    "${OBJECT_CONNECT_DXC_REDIST_DIR}/dxil.dll"
)
foreach(_required_path IN LISTS OBJECT_CONNECT_DXC_RUNTIME_FILES)
    if(NOT EXISTS "${_required_path}")
        message(FATAL_ERROR
            "The DirectX Shader Compiler runtime directory is incomplete. "
            "Expected file:\n  ${_required_path}"
        )
    endif()
endforeach()

set(_required_project_resources
    "${OBJECT_CONNECT_RESOURCE_DIR}/white1x1.png"
    "${OBJECT_CONNECT_RESOURCE_DIR}/kamata.ico"
    "${OBJECT_CONNECT_RESOURCE_DIR}/data/levels.csv"
    "${OBJECT_CONNECT_RESOURCE_DIR}/data/nodes.csv"
    "${OBJECT_CONNECT_RESOURCE_DIR}/assets/textures/mouse/mouse_icon_01.png"
    "${OBJECT_CONNECT_RESOURCE_DIR}/assets/textures/mouse/mouse_icon_02.png"
    "${OBJECT_CONNECT_RESOURCE_DIR}/assets/textures/mouse/mouse_icon_03.png"
    "${OBJECT_CONNECT_RESOURCE_DIR}/assets/textures/node/heart.png"
    "${OBJECT_CONNECT_RESOURCE_DIR}/assets/textures/node/lung.png"
    "${OBJECT_CONNECT_RESOURCE_DIR}/assets/textures/node/liver.png"
    "${OBJECT_CONNECT_RESOURCE_DIR}/assets/textures/node/kidney.png"
    "${OBJECT_CONNECT_RESOURCE_DIR}/assets/textures/node/brain.png"
    "${OBJECT_CONNECT_RESOURCE_DIR}/assets/textures/node/stomach.png"
    "${OBJECT_CONNECT_RESOURCE_DIR}/assets/textures/node/organ.png"
    "${OBJECT_CONNECT_RESOURCE_DIR}/audio/bgm_start.wav"
    "${OBJECT_CONNECT_RESOURCE_DIR}/audio/bgm_loop.wav"
    "${OBJECT_CONNECT_RESOURCE_DIR}/audio/line_hold.wav"
    "${OBJECT_CONNECT_RESOURCE_DIR}/audio/line_relax.wav"
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
    "${OBJECT_CONNECT_RESOURCE_DIR}/shaders/Text.hlsli"
    "${OBJECT_CONNECT_RESOURCE_DIR}/shaders/TextVS.hlsl"
    "${OBJECT_CONNECT_RESOURCE_DIR}/shaders/TextPS.hlsl"
)
set(_bundled_stage_ids
    stage_01 stage_02 stage_03 stage_04 stage_05
    stage_06 stage_07 stage_08 stage_09 stage_10
    stage_11 stage_12 stage_13 stage_14 stage_15
    stage_16 stage_17 stage_18 stage_19 stage_20
)
foreach(_stage_id IN LISTS _bundled_stage_ids)
    list(APPEND _required_project_resources
        "${OBJECT_CONNECT_RESOURCE_DIR}/data/maps/${_stage_id}.csv"
    )
endforeach()
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
    src/ObjectConnect/Game/FinalResults.cpp
    src/ObjectConnect/Game/GameFlow.cpp
    src/ObjectConnect/Geometry/Geometry2D.cpp
    src/ObjectConnect/Puzzle/PuzzleBoard.cpp
    src/ObjectConnect/Tentacle/BloodTentacle.cpp
    src/ObjectConnect/Tentacle/RibbonStrip.cpp
    src/ObjectConnect/Text/TextLayout.cpp
    src/ObjectConnect/Text/Utf8.cpp
)

set(OBJECT_CONNECT_RUNTIME_SOURCES
    src/ObjectConnect/Audio/GameAudio.cpp
    src/ObjectConnect/Core/Application.cpp
    src/ObjectConnect/Core/FrameTimer.cpp
    src/ObjectConnect/Game/Game.cpp
    src/ObjectConnect/Input/InputSystem.cpp
    src/ObjectConnect/Rendering/GameUiRenderer.cpp
    src/ObjectConnect/Rendering/MouseCursorRenderer.cpp
    src/ObjectConnect/Rendering/PuzzleRenderer.cpp
    src/ObjectConnect/Rendering/TextureHandleRegistry.cpp
    src/ObjectConnect/Text/FontSystem.cpp
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
)
target_include_directories(object_connect_runtime SYSTEM PRIVATE "${IMGUI_INCLUDE_DIR}")
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
    COMMAND "${CMAKE_COMMAND}"
            "-DCONFIGURATION=$<CONFIG>"
            "-DOUTPUT_DIRECTORY=${OBJECT_CONNECT_OUTPUT_DIR}"
            "-DDXCOMPILER_SOURCE=${OBJECT_CONNECT_DXC_REDIST_DIR}/dxcompiler.dll"
            "-DDXIL_SOURCE=${OBJECT_CONNECT_DXC_REDIST_DIR}/dxil.dll"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/DeployDxcRuntime.cmake"
    DEPENDS
        ${OBJECT_CONNECT_RESOURCE_FILES}
        ${OBJECT_CONNECT_DXC_RUNTIME_FILES}
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/DeployDxcRuntime.cmake"
    COMMENT "Deploying Object_Connect resources and runtime dependencies"
    VERBATIM
)
add_dependencies(Object_Connect Object_Connect_Resources)

if(BUILD_TESTING)
    add_executable(Object_Connect_CoreTests
        tests/TestMain.cpp
        tests/TestSupport.hpp
        tests/Data/PuzzleCatalogTests.cpp
        tests/Game/FinalResultsTests.cpp
        tests/Game/GameFlowTests.cpp
        tests/Game/MusicSequencePolicyTests.cpp
        tests/Geometry/Geometry2DTests.cpp
        tests/Input/MouseCursorStateTests.cpp
        tests/Puzzle/PuzzleBoardTests.cpp
        tests/Tentacle/BloodTentacleTests.cpp
        tests/Tentacle/RibbonStripTests.cpp
        tests/Text/LazyGlyphTests.cpp
        tests/Text/TextLayoutTests.cpp
        tests/Text/Utf8Tests.cpp
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

    add_executable(Object_Connect_FontSystemLifecycleTests
        tests/TestSupport.hpp
        tests/Text/FontSystemLifecycleTests.cpp
    )
    target_include_directories(Object_Connect_FontSystemLifecycleTests
        PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/tests"
    )
    target_link_libraries(Object_Connect_FontSystemLifecycleTests
        PRIVATE ObjectConnect::Runtime
    )
    target_compile_definitions(Object_Connect_FontSystemLifecycleTests PRIVATE
        UNICODE _UNICODE NOMINMAX
    )
    configure_object_connect_target(Object_Connect_FontSystemLifecycleTests)
    set_target_properties(Object_Connect_FontSystemLifecycleTests PROPERTIES
        INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE
    )
    add_test(NAME Object_Connect.FontSystemLifecycle
             COMMAND Object_Connect_FontSystemLifecycleTests)

    add_executable(Object_Connect_GameAudioTests
        tests/TestSupport.hpp
        tests/Audio/GameAudioTests.cpp
    )
    target_include_directories(Object_Connect_GameAudioTests
        PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/tests"
    )
    target_link_libraries(Object_Connect_GameAudioTests
        PRIVATE ObjectConnect::Runtime
    )
    target_compile_definitions(Object_Connect_GameAudioTests PRIVATE
        UNICODE _UNICODE NOMINMAX
    )
    configure_object_connect_target(Object_Connect_GameAudioTests)
    set_target_properties(Object_Connect_GameAudioTests PROPERTIES
        INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE
    )
    add_test(NAME Object_Connect.GameAudio
             COMMAND Object_Connect_GameAudioTests)
endif()

add_custom_target(run
    COMMAND "$<TARGET_FILE:Object_Connect>"
    WORKING_DIRECTORY "$<TARGET_FILE_DIR:Object_Connect>"
    DEPENDS Object_Connect
    USES_TERMINAL
)
