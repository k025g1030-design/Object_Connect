cmake_minimum_required(VERSION 3.25)

foreach(_required_variable IN ITEMS
        CONFIGURATION
        OUTPUT_DIRECTORY
        DXCOMPILER_SOURCE
        DXIL_SOURCE)
    if(NOT DEFINED ${_required_variable} OR "${${_required_variable}}" STREQUAL "")
        message(FATAL_ERROR
            "DeployDxcRuntime.cmake requires ${_required_variable}."
        )
    endif()
endforeach()

file(MAKE_DIRECTORY "${OUTPUT_DIRECTORY}")

set(_dxcompiler_output "${OUTPUT_DIRECTORY}/dxcompiler.dll")
set(_dxil_output "${OUTPUT_DIRECTORY}/dxil.dll")

if(CONFIGURATION STREQUAL "Debug")
    foreach(_source IN ITEMS "${DXCOMPILER_SOURCE}" "${DXIL_SOURCE}")
        if(NOT EXISTS "${_source}")
            message(FATAL_ERROR "Required DirectX debug runtime was not found: ${_source}")
        endif()
    endforeach()

    file(COPY_FILE "${DXCOMPILER_SOURCE}" "${_dxcompiler_output}" ONLY_IF_DIFFERENT)
    file(COPY_FILE "${DXIL_SOURCE}" "${_dxil_output}" ONLY_IF_DIFFERENT)
else()
    # The application enables the DirectX debug layer only in Debug builds.
    # Remove stale sidecars so a previous build cannot leak them into a release.
    file(REMOVE "${_dxcompiler_output}" "${_dxil_output}")
endif()
