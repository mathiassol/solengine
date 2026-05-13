cmake_minimum_required(VERSION 3.20)
project(SolEngine
    VERSION 0.1.0
    DESCRIPTION "Sol game engine runtime: bgfx + EnTT + GLM + Jolt + ImGui"
    LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Use dynamic CRT (/MD) consistently across all targets (required when mixing DLLs)
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL" CACHE STRING "" FORCE)
endif()

# Single output dir so engine.exe and game DLLs land together
set(SOL_OUT ${CMAKE_BINARY_DIR}/out)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${SOL_OUT})
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${SOL_OUT})
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${SOL_OUT}/lib)

option(SOL_BUILD_DEMOS  "Build demo game"                                  ON)
option(SOL_MONOLITHIC   "Package game as a single standalone exe (no DLL)" OFF)

include(cmake/Dependencies.cmake)
include(cmake/SolGame.cmake)

add_subdirectory(engine)
add_subdirectory(runtime)

if(SOL_BUILD_DEMOS)
    add_subdirectory(demo)
endif()

option(SOL_BUILD_EDITOR "Build the Qt6 editor" ON)
if(SOL_BUILD_EDITOR)
    add_subdirectory(editor)
endif()

message(STATUS "")
message(STATUS "SolEngine configured.")
message(STATUS "  Output dir  : ${SOL_OUT}")
message(STATUS "  Demos       : ${SOL_BUILD_DEMOS}")
message(STATUS "  Monolithic  : ${SOL_MONOLITHIC}")
message(STATUS "  Editor      : ${SOL_BUILD_EDITOR}")
message(STATUS "")
