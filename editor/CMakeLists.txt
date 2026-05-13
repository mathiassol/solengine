# ---- sol_editor: Qt6 Widgets editor application --------------------------------
list(APPEND CMAKE_PREFIX_PATH "C:/Qt/6.7.3/msvc2019_64")
find_package(Qt6 REQUIRED COMPONENTS Widgets)

set(EDITOR_SOURCES
    src/main.cpp
    src/main_window.cpp
    src/viewport_widget.cpp
    src/panels/console_panel.cpp
    src/panels/hierarchy_panel.cpp
    src/panels/inspector_panel.cpp
    src/panels/asset_browser_panel.cpp
    src/widgets/number_field.h
    src/widgets/number_field.cpp
    src/widgets/vec3_field.h
    src/widgets/vec3_field.cpp
    src/widgets/toggle_switch.h
    src/widgets/toggle_switch.cpp
    src/widgets/section_header.h
    src/widgets/section_header.cpp
    src/widgets/asset_path_field.h
    src/widgets/asset_path_field.cpp
    resources.qrc
    src/style.qss
)

qt_add_executable(sol_editor ${EDITOR_SOURCES})

set_target_properties(sol_editor PROPERTIES
    AUTOMOC          ON
    AUTORCC          ON
    AUTOUIC          ON
    OUTPUT_NAME      "sol_editor"
    # GUI subsystem (no console) in Release; keep console in Debug for logging
    WIN32_EXECUTABLE "$<$<NOT:$<CONFIG:Debug>>:TRUE>"
)

target_include_directories(sol_editor PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(sol_editor PRIVATE
    engine_core
    Qt6::Widgets
)

# ---- Deploy Qt DLLs alongside the exe ----------------------------------------
find_program(WINDEPLOYQT_EXECUTABLE windeployqt
    HINTS "C:/Qt/6.7.3/msvc2019_64/bin"
)

if(WINDEPLOYQT_EXECUTABLE)
    add_custom_command(TARGET sol_editor POST_BUILD
        COMMAND "${WINDEPLOYQT_EXECUTABLE}"
                --no-translations
                --no-system-d3d-compiler
                --no-opengl-sw
                "$<TARGET_FILE:sol_editor>"
        COMMENT "Deploying Qt6 runtime for sol_editor"
        VERBATIM
    )
else()
    message(WARNING
        "windeployqt not found — Qt DLLs will not be auto-deployed.\n"
        "Add C:/Qt/6.7.3/msvc2019_64/bin to PATH or copy Qt DLLs manually."
    )
endif()
