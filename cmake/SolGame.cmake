# cmake/SolGame.cmake
# sol_add_game(TARGET <name> SOURCES <src...>)
#
# Normal mode    (-DSOL_MONOLITHIC=OFF): creates <name>.dll loaded by sol.exe
# Monolithic mode (-DSOL_MONOLITHIC=ON): creates <name>.exe with engine baked in

function(sol_add_game)
    cmake_parse_arguments(ARG "" "TARGET" "SOURCES" ${ARGN})

    if(NOT ARG_TARGET)
        message(FATAL_ERROR "sol_add_game: TARGET is required")
    endif()
    if(NOT ARG_SOURCES)
        message(FATAL_ERROR "sol_add_game: SOURCES is required")
    endif()

    if(SOL_MONOLITHIC)
        # --- Standalone exe ---
        set(_mono_main "${CMAKE_SOURCE_DIR}/engine/src/main_monolithic.cpp")
        add_executable(${ARG_TARGET} ${ARG_SOURCES} "${_mono_main}")
        target_link_libraries(${ARG_TARGET} PRIVATE engine_core glfw)
        # engine_core is STATIC in monolithic mode — propagate the flag
        target_compile_definitions(${ARG_TARGET} PRIVATE SOL_STATIC_BUILD=1)
        set_target_properties(${ARG_TARGET} PROPERTIES
            OUTPUT_NAME  "${ARG_TARGET}"
            RUNTIME_OUTPUT_DIRECTORY "${SOL_OUT}")
        message(STATUS "[SolEngine] ${ARG_TARGET} → standalone exe (monolithic)")
    else()
        # --- Shared DLL ---
        add_library(${ARG_TARGET} SHARED ${ARG_SOURCES})
        target_link_libraries(${ARG_TARGET} PRIVATE engine_core glfw)
        set_target_properties(${ARG_TARGET} PROPERTIES
            OUTPUT_NAME "${ARG_TARGET}"
            PREFIX       ""
            RUNTIME_OUTPUT_DIRECTORY "${SOL_OUT}")
        message(STATUS "[SolEngine] ${ARG_TARGET} → game DLL")
    endif()
endfunction()
