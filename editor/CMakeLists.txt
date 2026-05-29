# ---- sol_editor: Qt6 Widgets editor application --------------------------------
# Resolve Qt6 install directory:
#   1. Use the Qt6_DIR CMake variable if the caller set it
#   2. Fall back to the QT_DIR / Qt6_DIR environment variables
#   3. Fall back to the legacy hardcoded default so existing setups keep working
if(DEFINED Qt6_DIR)
    # already set by -DQt6_DIR=... on the cmake command line — nothing to do
elseif(DEFINED ENV{Qt6_DIR})
    list(APPEND CMAKE_PREFIX_PATH "$ENV{Qt6_DIR}")
elseif(DEFINED ENV{QT_DIR})
    list(APPEND CMAKE_PREFIX_PATH "$ENV{QT_DIR}")
else()
    # Legacy fallback — keeps existing developer machines working unchanged
    list(APPEND CMAKE_PREFIX_PATH "C:/Qt/6.7.3/msvc2019_64")
endif()

find_package(Qt6 REQUIRED COMPONENTS Widgets)

set(EDITOR_SOURCES
    src/main.cpp
    src/main_window.cpp
    src/viewport_widget.cpp
    src/editor_ui.cpp
    src/project_launcher.h
    src/project_launcher.cpp
    src/undo_redo.cpp
    src/style/ue5_theme.cpp
    src/style/sol_widgets.cpp
    resources.qrc
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
    dwmapi
)

# ---- Deploy Qt DLLs alongside the exe ----------------------------------------
# Look for windeployqt relative to the Qt install found by find_package,
# then fall back to a plain PATH search so any Qt install works.
get_target_property(_qt6_qmake Qt6::qmake IMPORTED_LOCATION)
if(_qt6_qmake)
    get_filename_component(_qt6_bin_dir "${_qt6_qmake}" DIRECTORY)
else()
    set(_qt6_bin_dir "")
endif()

find_program(WINDEPLOYQT_EXECUTABLE windeployqt
    HINTS "${_qt6_bin_dir}"
          "$ENV{Qt6_DIR}/bin"
          "$ENV{QT_DIR}/bin"
          "C:/Qt/6.7.3/msvc2019_64/bin"
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
        "Set Qt6_DIR (env or CMake variable) to your Qt install, e.g.:\n"
        "  cmake -DQt6_DIR=C:/Qt/6.7.3/msvc2019_64/lib/cmake/Qt6 ..."
    )
endif()

# ---- Copy FontAwesome icons alongside the editor exe -----------------------
add_custom_command(TARGET sol_editor POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
            "$<TARGET_FILE_DIR:sol_editor>/fonts"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/fonts/fa-solid-900.ttf"
            "$<TARGET_FILE_DIR:sol_editor>/fonts/fa-solid-900.ttf"
    COMMENT "Copying FontAwesome font to output directory"
    VERBATIM
)
